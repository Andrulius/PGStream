"use strict";

const els = {
  start: document.getElementById("startButton"),
  nerd: document.getElementById("nerdButton"),
  nerdPanel: document.getElementById("nerdPanel"),
  bitrateSelect: document.getElementById("bitrateSelect"),
  latencySelect: document.getElementById("latencySelect"),
  autoModeSelect: document.getElementById("autoModeSelect"),
  autoButton: document.getElementById("autoButton"),
  remoteAudio: document.getElementById("remoteAudio"),
  connection: document.getElementById("connection"),
  transport: document.getElementById("transport"),
  codec: document.getElementById("codec"),
  bitrate: document.getElementById("bitrate"),
  warning: document.getElementById("warning"),
  rtcConnectionState: document.getElementById("rtcConnectionState"),
  iceConnectionState: document.getElementById("iceConnectionState"),
  actualBitrate: document.getElementById("actualBitrate"),
  packetsReceived: document.getElementById("packetsReceived"),
  packetsLost: document.getElementById("packetsLost"),
  jitter: document.getElementById("jitter"),
  roundTripTime: document.getElementById("roundTripTime"),
  concealedSamples: document.getElementById("concealedSamples"),
  jitterBufferDelay: document.getElementById("jitterBufferDelay"),
  jitterBufferTargetDelay: document.getElementById("jitterBufferTargetDelay"),
  remoteTrackState: document.getElementById("remoteTrackState"),
  latencyPreset: document.getElementById("latencyPreset"),
  senderQueueFill: document.getElementById("senderQueueFill"),
  senderDroppedFrames: document.getElementById("senderDroppedFrames"),
  encoderOverloads: document.getElementById("encoderOverloads"),
  webrtcOpenTracks: document.getElementById("webrtcOpenTracks"),
  signalingState: document.getElementById("signalingState"),
  rtpSent: document.getElementById("rtpSent"),
  rtpFailures: document.getElementById("rtpFailures"),
  rtpAttempts: document.getElementById("rtpAttempts"),
  autoActive: document.getElementById("autoActive"),
  autoMode: document.getElementById("autoMode"),
  autoProfile: document.getElementById("autoProfile"),
  autoPhase: document.getElementById("autoPhase"),
  autoElapsed: document.getElementById("autoElapsed"),
  autoStableTime: document.getElementById("autoStableTime"),
  autoBaselinePacketsLost: document.getElementById("autoBaselinePacketsLost"),
  autoCurrentPacketsLost: document.getElementById("autoCurrentPacketsLost"),
  autoLossDelta: document.getElementById("autoLossDelta"),
  autoProfileIndex: document.getElementById("autoProfileIndex"),
  activeEncoderBitrate: document.getElementById("activeEncoderBitrate"),
  activeOpusFrame: document.getElementById("activeOpusFrame"),
  autoProfileChanges: document.getElementById("autoProfileChanges"),
  autoFifoUnderruns: document.getElementById("autoFifoUnderruns"),
  autoFinalProfile: document.getElementById("autoFinalProfile"),
  autoLastFailure: document.getElementById("autoLastFailure")
};

const defaultWarning = els.warning.textContent.trim();
const autoWarmupMs = 1000;
const autoSuccessMs = 3000;
const autoStatsUnavailableMs = 10000;
const bitrateProfiles = [510000, 320000, 256000, 192000, 128000];
const latencyProfiles = ["ultra", "low", "balanced", "safe"];
const autoMaxProfiles = bitrateProfiles.length + latencyProfiles.length - 1;

let socket = null;
let pc = null;
let remoteStream = null;
let remoteTrack = null;
let uiTimer = null;
let serverTimer = null;
let rtcStatsTimer = null;
let running = false;
let stopping = false;
let suppressControlEvents = 0;
let uiSyncSuppressedEventCount = 0;
let lastStateRevision = 0;
let lastStateUpdateReceivedTimestamp = "-";

const client = {
  status: "stopped"
};

const rtcStats = {
  connectionState: "closed",
  iceConnectionState: "closed",
  codec: "opus/48000/2",
  packetsReceived: 0,
  bytesReceived: 0,
  packetsLost: 0,
  packetsDiscarded: null,
  inboundStatsId: null,
  packetsReceivedIncreasing: false,
  jitterMs: 0,
  roundTripTimeMs: null,
  concealedSamples: null,
  concealmentEvents: null,
  audioLevel: null,
  jitterBufferDelayMs: null,
  jitterBufferTargetDelayMs: null,
  actualBitrateKbps: 0,
  lastBytes: 0,
  lastBytesAt: 0
};

const server = {
  transport: "WebRTC Opus",
  opusBitrateBps: 320000,
  opusBitratePreset: "320 kb/s High Quality / Recommended",
  latencyPreset: "Balanced",
  selectedOpusBitrateBps: 320000,
  selectedOpusBitratePreset: "320 kb/s High Quality / Recommended",
  selectedLatencyPreset: "Balanced",
  stateRevision: 0,
  stateOrigin: "startup",
  lastAdaptationReason: "startup",
  lastAdaptationTimestamp: "-",
  activePlayoutDelayHintMs: 80,
  latencyTarget: "about 40-90 ms",
  opusFrameDurationMs: 20,
  senderQueueFillMs: 0,
  senderDroppedFrames: 0,
  fifoUnderruns: 0,
  encoderOverloads: 0,
  webrtcOpenTracks: 0,
  rtpAttempts: 0,
  rtpSent: 0,
  rtpFailures: 0
};

let activeProfile = makeProfile(320000, "balanced", "initial");
let profileApplyInProgress = false;
let profileApplyDeadlineMs = 0;
let profileUserSelected = false;

const autoNegotiation = {
  active: false,
  mode: "balanced",
  state: "inactive",
  bitrateIndex: 0,
  latencyIndex: 0,
  balanceNext: "latency",
  inboundStatsId: null,
  baselinePacketsLost: null,
  currentPacketsLost: null,
  currentPacketsReceived: null,
  phaseStartedAt: 0,
  waitingStartedAt: 0,
  warmupStartedAt: 0,
  evaluationStartedAt: 0,
  elapsedMs: 0,
  stableMs: 0,
  lossDelta: 0,
  profileChanges: 0,
  profilesTested: 0,
  finalProfile: "-",
  lastFailure: "-",
  readiness: "not started",
  transitioning: false
};

function setStatus(status) {
  client.status = status;
}

function resetClientStats() {
  client.status = "stopped";
  rtcStats.connectionState = "closed";
  rtcStats.iceConnectionState = "closed";
  rtcStats.packetsReceived = 0;
  rtcStats.bytesReceived = 0;
  rtcStats.packetsLost = 0;
  rtcStats.packetsDiscarded = null;
  rtcStats.inboundStatsId = null;
  rtcStats.packetsReceivedIncreasing = false;
  rtcStats.jitterMs = 0;
  rtcStats.roundTripTimeMs = null;
  rtcStats.concealedSamples = null;
  rtcStats.concealmentEvents = null;
  rtcStats.audioLevel = null;
  rtcStats.jitterBufferDelayMs = null;
  rtcStats.jitterBufferTargetDelayMs = null;
  rtcStats.actualBitrateKbps = 0;
  rtcStats.lastBytes = 0;
  rtcStats.lastBytesAt = 0;
  remoteTrack = null;
}

function bitrateLabel(bps) {
  if (bps === 128000) return "128 kb/s Good Preview";
  if (bps === 192000) return "192 kb/s Very Good";
  if (bps === 256000) return "256 kb/s Studio Preview";
  if (bps === 510000) return "510 kb/s Experimental Max";
  return "320 kb/s High Quality / Recommended";
}

function latencyLabel(value) {
  if (value === "safe") return "Safe";
  if (value === "low") return "Low Latency";
  if (value === "ultra") return "Ultra Low / Experimental";
  return "Balanced";
}

function latencyTarget(value) {
  if (value === "safe") return "about 80-150 ms";
  if (value === "low") return "about 25-60 ms";
  if (value === "ultra") return "about 15-40 ms experimental";
  return "about 40-90 ms";
}

function latencyFrameMs(value) {
  if (value === "ultra") return 10;
  if (value === "low") return 10;
  return 20;
}

function normalizeBitrateBps(value) {
  const parsed = Number(value);
  return bitrateProfiles.includes(parsed) ? parsed : 320000;
}

function normalizeLatencyValue(value) {
  const text = String(value || "").toLowerCase();
  return latencyProfiles.includes(text) ? text : "balanced";
}

function latencyValueFromPreset(preset) {
  const lower = String(preset || "").toLowerCase();
  if (lower.includes("ultra")) return "ultra";
  if (lower.includes("low")) return "low";
  if (lower.includes("safe")) return "safe";
  return "balanced";
}

function makeProfile(bitrateBps, latencyValue, source = "ui", target = null, frameMs = null) {
  const normalizedBitrate = normalizeBitrateBps(bitrateBps);
  const normalizedLatency = normalizeLatencyValue(latencyValue);
  return {
    bitrateBps: normalizedBitrate,
    bitratePreset: bitrateLabel(normalizedBitrate),
    latencyValue: normalizedLatency,
    latencyPreset: latencyLabel(normalizedLatency),
    latencyTarget: target || latencyTarget(normalizedLatency),
    opusFrameDurationMs: Number.isFinite(Number(frameMs)) ? Number(frameMs) : latencyFrameMs(normalizedLatency),
    source
  };
}

function profileFromControls() {
  return makeProfile(els.bitrateSelect.value, els.latencySelect.value, "controls");
}

function profileFromServerInfo(info) {
  if (!info || !info.opusBitrateBps || !info.latencyPreset) return null;
  return makeProfile(
    info.opusBitrateBps,
    latencyValueFromPreset(info.latencyPreset),
    "server",
    info.latencyTarget,
    info.opusFrameDurationMs
  );
}

function profileMatches(left, right) {
  return !!left
    && !!right
    && normalizeBitrateBps(left.bitrateBps) === normalizeBitrateBps(right.bitrateBps)
    && normalizeLatencyValue(left.latencyValue) === normalizeLatencyValue(right.latencyValue);
}

function setControlsFromProfile(profile) {
  suppressControlEvents += 1;
  try {
    els.bitrateSelect.value = String(normalizeBitrateBps(profile.bitrateBps));
    els.latencySelect.value = normalizeLatencyValue(profile.latencyValue);
  } finally {
    suppressControlEvents -= 1;
  }
}

function setActiveProfile(profile, options = {}) {
  activeProfile = makeProfile(
    profile.bitrateBps,
    profile.latencyValue,
    profile.source || "ui",
    profile.latencyTarget,
    profile.opusFrameDurationMs
  );

  if (options.updateControls !== false) {
    setControlsFromProfile(activeProfile);
  }
}

function activeProfileLabel(profile = activeProfile) {
  return `${profile.bitratePreset} / ${profile.latencyPreset}`;
}

function formatPacketCount(value) {
  return Number.isFinite(value) ? String(value) : "-";
}

function formatBitrateKbps(value) {
  const bps = Number(value);
  return Number.isFinite(bps) ? `${(bps / 1000).toFixed(0)} kb/s` : "-";
}

function markProfileApplyPending() {
  profileApplyInProgress = true;
  profileApplyDeadlineMs = performance.now() + 8000;
}

function autoModeLabel(value) {
  if (value === "quality") return "Quality Priority";
  if (value === "latency") return "Latency Priority";
  return "Balanced";
}

function profileLabel(bitrateIndex = autoNegotiation.bitrateIndex, latencyIndex = autoNegotiation.latencyIndex) {
  return activeProfileLabel(makeProfile(bitrateProfiles[bitrateIndex], latencyProfiles[latencyIndex], "auto"));
}

function profileFromAutoIndices() {
  return makeProfile(
    bitrateProfiles[autoNegotiation.bitrateIndex],
    latencyProfiles[autoNegotiation.latencyIndex],
    "auto"
  );
}

function applyAutoProfile() {
  setActiveProfile(profileFromAutoIndices());
}

function resetAutoMeasurement(state = "connecting") {
  const now = performance.now();
  autoNegotiation.state = state;
  autoNegotiation.inboundStatsId = null;
  autoNegotiation.baselinePacketsLost = null;
  autoNegotiation.currentPacketsLost = null;
  autoNegotiation.currentPacketsReceived = null;
  autoNegotiation.phaseStartedAt = now;
  autoNegotiation.waitingStartedAt = now;
  autoNegotiation.warmupStartedAt = 0;
  autoNegotiation.evaluationStartedAt = 0;
  autoNegotiation.elapsedMs = 0;
  autoNegotiation.stableMs = 0;
  autoNegotiation.lossDelta = 0;
  autoNegotiation.readiness = "waiting for peer";
}

function nextAutoProfile() {
  const atLowestBitrate = autoNegotiation.bitrateIndex >= bitrateProfiles.length - 1;
  const atSafestLatency = autoNegotiation.latencyIndex >= latencyProfiles.length - 1;

  if (autoNegotiation.mode === "quality") {
    if (!atSafestLatency) {
      autoNegotiation.latencyIndex += 1;
      return true;
    }

    if (!atLowestBitrate) {
      autoNegotiation.bitrateIndex += 1;
      autoNegotiation.latencyIndex = latencyProfiles.length - 1;
      return true;
    }

    return false;
  }

  if (autoNegotiation.mode === "latency") {
    if (!atLowestBitrate) {
      autoNegotiation.bitrateIndex += 1;
      return true;
    }

    if (!atSafestLatency) {
      autoNegotiation.latencyIndex += 1;
      return true;
    }

    return false;
  }

  const tryLatency = () => {
    if (autoNegotiation.latencyIndex >= latencyProfiles.length - 1) return false;
    autoNegotiation.latencyIndex += 1;
    return true;
  };
  const tryBitrate = () => {
    if (autoNegotiation.bitrateIndex >= bitrateProfiles.length - 1) return false;
    autoNegotiation.bitrateIndex += 1;
    return true;
  };

  const adjusted = autoNegotiation.balanceNext === "latency"
    ? (tryLatency() || tryBitrate())
    : (tryBitrate() || tryLatency());

  autoNegotiation.balanceNext = autoNegotiation.balanceNext === "latency" ? "bitrate" : "latency";
  return adjusted;
}

function setAutoWarning(text) {
  els.warning.textContent = text || defaultWarning;
}

function peerReadyReason(inbound) {
  const connectionState = pc ? pc.connectionState : rtcStats.connectionState;
  const iceState = pc ? pc.iceConnectionState : rtcStats.iceConnectionState;

  if (connectionState !== "connected") {
    return `waiting for peer connection (${connectionState || "unknown"})`;
  }

  if (iceState !== "connected" && iceState !== "completed") {
    return `waiting for ICE (${iceState || "unknown"})`;
  }

  if (!remoteTrack || remoteTrack.readyState !== "live") {
    return "waiting for live remote audio track";
  }

  if (!inbound) {
    return "waiting for inbound-rtp audio stats";
  }

  if (!Number.isFinite(inbound.packetsLost)) {
    return "waiting for readable packetsLost";
  }

  if (!Number.isFinite(inbound.packetsReceived)) {
    return "waiting for readable packetsReceived";
  }

  if (inbound.packetsReceived <= 0) {
    return "waiting for first received audio packet";
  }

  if (!inbound.packetsReceivedIncreasing) {
    return "waiting for packetsReceived to increase";
  }

  return "";
}

function stopAutoNegotiationForStats(reason) {
  autoNegotiation.active = false;
  autoNegotiation.state = "stats unavailable";
  autoNegotiation.lastFailure = reason;
  autoNegotiation.readiness = reason;
  setAutoWarning(`Auto negotiation stopped: ${reason}.`);
  renderUi();
}

function beginAutoWaiting(reason) {
  const now = performance.now();
  if (autoNegotiation.state !== "waiting for peer") {
    autoNegotiation.state = "waiting for peer";
    autoNegotiation.phaseStartedAt = now;
    autoNegotiation.waitingStartedAt = now;
    autoNegotiation.elapsedMs = 0;
    autoNegotiation.stableMs = 0;
    autoNegotiation.lossDelta = 0;
    autoNegotiation.inboundStatsId = null;
    autoNegotiation.baselinePacketsLost = null;
  }

  autoNegotiation.readiness = reason;
  autoNegotiation.elapsedMs = now - autoNegotiation.phaseStartedAt;

  if (now - autoNegotiation.waitingStartedAt >= autoStatsUnavailableMs) {
    stopAutoNegotiationForStats(reason);
  } else {
    renderUi();
  }
}

async function reconnectForAutoProfile() {
  autoNegotiation.transitioning = true;
  resetAutoMeasurement("reconnecting");
  markProfileApplyPending();
  renderUi();

  try {
    if (running || stopping) {
      await stopAudio("auto reconnect", { preserveAuto: true });
    }

    if (autoNegotiation.active) {
      await startAudio({ preserveAuto: true });
      resetAutoMeasurement("waiting for peer");
      setAutoWarning(`Auto negotiation testing: ${profileLabel()}`);
    }
  } finally {
    autoNegotiation.transitioning = false;
    renderUi();
  }
}

async function startAutoNegotiation() {
  if (autoNegotiation.active) {
    cancelAutoNegotiation("cancelled");
    return;
  }

  autoNegotiation.active = true;
  autoNegotiation.mode = els.autoModeSelect.value;
  autoNegotiation.bitrateIndex = 0;
  autoNegotiation.latencyIndex = 0;
  autoNegotiation.balanceNext = "latency";
  autoNegotiation.profileChanges = 0;
  autoNegotiation.profilesTested = 1;
  autoNegotiation.finalProfile = "-";
  autoNegotiation.lastFailure = "-";
  resetAutoMeasurement("starting");
  applyAutoProfile();
  setAutoWarning(`Auto negotiation starting: ${profileLabel()}`);
  await reconnectForAutoProfile();
}

function completeAutoNegotiation() {
  autoNegotiation.active = false;
  autoNegotiation.state = "complete";
  autoNegotiation.finalProfile = profileLabel();
  profileUserSelected = true;
  setAutoWarning(`Auto negotiation complete: ${autoNegotiation.finalProfile}`);
  renderUi();
}

async function failAutoNegotiation(delta) {
  autoNegotiation.state = "profile failed";
  autoNegotiation.lastFailure = `${profileLabel()} lost ${delta} packet${delta === 1 ? "" : "s"} during measurement`;
  renderUi();

  if (!nextAutoProfile()) {
    autoNegotiation.bitrateIndex = bitrateProfiles.length - 1;
    autoNegotiation.latencyIndex = latencyProfiles.length - 1;
    applyAutoProfile();
    autoNegotiation.active = false;
    autoNegotiation.state = "failed all profiles";
    autoNegotiation.finalProfile = profileLabel();
    setAutoWarning("Auto negotiation could not find a zero-loss profile. Using safest available settings.");
    renderUi();
    return;
  }

  autoNegotiation.profileChanges += 1;
  autoNegotiation.profilesTested += 1;
  applyAutoProfile();
  setAutoWarning(`Auto negotiation retrying: ${profileLabel()}`);
  await reconnectForAutoProfile();
}

function cancelAutoNegotiation(reason = "cancelled") {
  if (!autoNegotiation.active) return;

  autoNegotiation.active = false;
  autoNegotiation.state = reason;
  autoNegotiation.finalProfile = reason === "cancelled" ? "-" : autoNegotiation.finalProfile;
  autoNegotiation.readiness = reason;
  setAutoWarning(reason === "cancelled" ? "Auto negotiation cancelled." : defaultWarning);
  renderUi();
}

async function updateAutoNegotiation(inbound) {
  if (!autoNegotiation.active || autoNegotiation.transitioning) return;

  const now = performance.now();
  const notReadyReason = peerReadyReason(inbound);
  if (notReadyReason) {
    beginAutoWaiting(notReadyReason);
    return;
  }

  const currentPacketsLost = inbound.packetsLost;
  autoNegotiation.currentPacketsLost = currentPacketsLost;
  autoNegotiation.currentPacketsReceived = inbound.packetsReceived;
  autoNegotiation.readiness = "ready";

  if (autoNegotiation.state === "connecting"
      || autoNegotiation.state === "starting"
      || autoNegotiation.state === "reconnecting"
      || autoNegotiation.state === "waiting for peer"
      || autoNegotiation.state === "profile failed") {
    autoNegotiation.state = "warmup";
    autoNegotiation.phaseStartedAt = now;
    autoNegotiation.inboundStatsId = inbound.id;
    autoNegotiation.warmupStartedAt = now;
    autoNegotiation.elapsedMs = 0;
    autoNegotiation.stableMs = 0;
    autoNegotiation.lossDelta = 0;
    renderUi();
    return;
  }

  if (autoNegotiation.state === "warmup") {
    if (inbound.id !== autoNegotiation.inboundStatsId) {
      resetAutoMeasurement("waiting for peer");
      autoNegotiation.readiness = "inbound stats source changed before baseline";
      renderUi();
      return;
    }

    autoNegotiation.elapsedMs = now - autoNegotiation.warmupStartedAt;
    if (autoNegotiation.elapsedMs >= autoWarmupMs) {
      autoNegotiation.state = "measuring";
      autoNegotiation.phaseStartedAt = now;
      autoNegotiation.baselinePacketsLost = currentPacketsLost;
      autoNegotiation.evaluationStartedAt = now;
      autoNegotiation.elapsedMs = 0;
      autoNegotiation.stableMs = 0;
      autoNegotiation.lossDelta = 0;
    }
    renderUi();
    return;
  }

  if (autoNegotiation.state !== "measuring") return;

  if (inbound.id !== autoNegotiation.inboundStatsId) {
    resetAutoMeasurement("waiting for peer");
    autoNegotiation.readiness = "inbound stats source changed during measurement";
    renderUi();
    return;
  }

  autoNegotiation.elapsedMs = now - autoNegotiation.evaluationStartedAt;
  const delta = currentPacketsLost - autoNegotiation.baselinePacketsLost;

  if (!Number.isFinite(delta) || delta < 0) {
    resetAutoMeasurement("waiting for peer");
    autoNegotiation.readiness = "packetsLost counter reset before evaluation completed";
    renderUi();
    return;
  }

  autoNegotiation.lossDelta = delta;
  autoNegotiation.stableMs = delta === 0 ? autoNegotiation.elapsedMs : 0;

  if (autoNegotiation.lossDelta > 0) {
    await failAutoNegotiation(autoNegotiation.lossDelta);
    return;
  }

  if (autoNegotiation.elapsedMs >= autoSuccessMs) {
    completeAutoNegotiation();
    return;
  }

  renderUi();
}

function socketStateName() {
  if (!socket) return "closed";
  if (socket.readyState === WebSocket.CONNECTING) return "connecting";
  if (socket.readyState === WebSocket.OPEN) return "open";
  if (socket.readyState === WebSocket.CLOSING) return "closing";
  return "closed";
}

function fetchInfo() {
  return fetch("/info", { cache: "no-store" }).then((response) => {
    if (!response.ok) {
      throw new Error(`info request failed: ${response.status}`);
    }

    return response.json();
  });
}

function applyInfo(info) {
  if (info.transport) server.transport = info.transport;
  if (info.opusBitrateBps) server.opusBitrateBps = Number(info.opusBitrateBps);
  if (info.opusBitratePreset) server.opusBitratePreset = info.opusBitratePreset;
  if (info.selectedOpusBitrateBps) server.selectedOpusBitrateBps = Number(info.selectedOpusBitrateBps);
  if (info.selectedOpusBitratePreset) server.selectedOpusBitratePreset = info.selectedOpusBitratePreset;
  if (info.selectedLatencyPreset) server.selectedLatencyPreset = info.selectedLatencyPreset;
  if (info.latencyPreset) server.latencyPreset = info.latencyPreset;
  if (info.latencyTarget) server.latencyTarget = info.latencyTarget;
  if (info.opusFrameDurationMs) server.opusFrameDurationMs = Number(info.opusFrameDurationMs);
  if (info.activePlayoutDelayHintMs) server.activePlayoutDelayHintMs = Number(info.activePlayoutDelayHintMs);
  if (Number.isFinite(Number(info.stateRevision))) server.stateRevision = Number(info.stateRevision);
  if (info.stateOrigin) server.stateOrigin = info.stateOrigin;
  if (info.lastAdaptationReason) server.lastAdaptationReason = info.lastAdaptationReason;
  if (info.lastAdaptationTimestamp) server.lastAdaptationTimestamp = info.lastAdaptationTimestamp;

  const serverProfile = profileFromServerInfo(info);
  if (serverProfile) {
    const now = performance.now();
    if (profileApplyInProgress && profileMatches(serverProfile, activeProfile)) {
      profileApplyInProgress = false;
    } else if (profileApplyInProgress && now >= profileApplyDeadlineMs) {
      profileApplyInProgress = false;
    }

    if (!profileApplyInProgress || profileMatches(serverProfile, activeProfile)) {
      setActiveProfile(serverProfile);
    }
  }

  if (info.server) {
    server.senderQueueFillMs = Number(info.server.senderQueueFillMs || 0);
    server.senderDroppedFrames = Number(info.server.senderDroppedFrames || 0);
    server.fifoUnderruns = Number(info.server.serverFifoUnderruns || 0);
    server.encoderOverloads = Number(info.server.webrtcEncodeOverBudgetCount
      || info.server.webrtcEncoderOverloadWarnings
      || 0);
    server.webrtcOpenTracks = Number(info.server.webrtcOpenTracks || 0);
    server.rtpAttempts = Number(info.server.webrtcRtpPacketsAttempted || 0);
    server.rtpSent = Number(info.server.webrtcRtpPacketsSent || info.server.webrtcSendCalls || 0);
    server.rtpFailures = Number(info.server.webrtcRtpSendFailures || 0);

    if (info.server.webrtcConnectionState) {
      rtcStats.connectionState = info.server.webrtcConnectionState;
    }

    if (info.server.webrtcIceConnectionState) {
      rtcStats.iceConnectionState = info.server.webrtcIceConnectionState;
    }
  }
}

function applyStateUpdate(message) {
  const revision = Number(message.stateRevision || 0);
  if (revision > 0 && revision < lastStateRevision) {
    return;
  }

  lastStateRevision = revision || lastStateRevision;
  lastStateUpdateReceivedTimestamp = new Date().toISOString();

  server.stateRevision = lastStateRevision;
  server.stateOrigin = message.origin || server.stateOrigin;
  server.opusBitrateBps = Number(message.activeBitrateBps || server.opusBitrateBps);
  server.opusBitratePreset = message.activeBitrateLabel || bitrateLabel(server.opusBitrateBps);
  server.latencyPreset = message.activeLatencyMode || server.latencyPreset;
  server.opusFrameDurationMs = Number(message.activeFrameDurationMs || server.opusFrameDurationMs);
  server.activePlayoutDelayHintMs = Number(message.activePlayoutDelayHintMs || server.activePlayoutDelayHintMs);
  server.lastAdaptationReason = message.lastAdaptationReason || server.lastAdaptationReason;
  server.lastAdaptationTimestamp = message.lastAdaptationTimestamp || server.lastAdaptationTimestamp;

  const confirmed = makeProfile(
    server.opusBitrateBps,
    latencyValueFromPreset(server.latencyPreset),
    message.origin || "remote_sync",
    latencyTarget(latencyValueFromPreset(server.latencyPreset)),
    server.opusFrameDurationMs
  );
  setActiveProfile(confirmed);
  profileApplyInProgress = false;
  renderUi();
}

async function refreshServerInfo() {
  try {
    applyInfo(await fetchInfo());
    renderUi();
  } catch {
    // Transient polling failures are surfaced by socket/RTC state.
  }
}

function startTimers() {
  if (!uiTimer) {
    uiTimer = window.setInterval(renderUi, 250);
  }

  if (!rtcStatsTimer) {
    rtcStatsTimer = window.setInterval(refreshRtcStats, 1000);
  }

  startServerTimer();
}

function startServerTimer() {
  if (!serverTimer) {
    serverTimer = window.setInterval(refreshServerInfo, 1000);
  }
}

function stopTimers() {
  if (uiTimer) {
    window.clearInterval(uiTimer);
    uiTimer = null;
  }

  if (rtcStatsTimer) {
    window.clearInterval(rtcStatsTimer);
    rtcStatsTimer = null;
  }
}

function sendJson(message) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(message));
  }
}

async function handleSignalMessage(message) {
  if (!message || typeof message !== "object") return;

  if (message.type === "webrtc-answer" && pc) {
    await pc.setRemoteDescription({ type: message.sdpType || "answer", sdp: message.sdp });
    return;
  }

  if (message.type === "webrtc-candidate" && pc && message.candidate) {
    await pc.addIceCandidate({ candidate: message.candidate, sdpMid: message.mid || "audio" });
    return;
  }

  if (message.type === "webrtc-error") {
    setStatus("error");
    els.warning.textContent = message.message || "WebRTC signaling failed";
  }

  if (message.type === "stream_state_update") {
    applyStateUpdate(message);
  }
}

function handleSocketMessage(event) {
  if (typeof event.data !== "string") return;

  try {
    handleSignalMessage(JSON.parse(event.data)).catch((error) => {
      els.warning.textContent = error.message;
      setStatus("error");
    });
  } catch {
    // Ignore non-JSON control text.
  }
}

function createSocket() {
  const wsUrl = `ws://${window.location.host}/ws`;
  socket = new WebSocket(wsUrl);
  socket.onmessage = handleSocketMessage;
  socket.onerror = () => {
    if (running && !stopping) {
      setStatus("error");
    }
  };
  socket.onclose = () => {
    if (running && !stopping) {
      stopAudio("error");
    }
  };
  return socket;
}

async function startWebRtc() {
  rtcStats.codec = "opus/48000/2";
  remoteStream = new MediaStream();
  els.remoteAudio.srcObject = remoteStream;
  markProfileApplyPending();

  pc = new RTCPeerConnection({ iceServers: [] });
  // Receive-only WebRTC playback: no microphone capture, so browser AEC/NS/AGC constraints are never enabled.
  pc.addTransceiver("audio", { direction: "recvonly" });
  els.remoteAudio.autoplay = true;
  els.remoteAudio.muted = false;
  els.remoteAudio.volume = 1.0;

  pc.ontrack = (event) => {
    if (event.track.kind !== "audio") return;
    remoteTrack = event.track;
    remoteTrack.onmute = renderUi;
    remoteTrack.onunmute = renderUi;
    remoteTrack.onended = renderUi;
    if (event.streams && event.streams[0]) {
      els.remoteAudio.srcObject = event.streams[0];
      remoteStream = event.streams[0];
    } else if (!remoteStream.getTracks().includes(event.track)) {
      remoteStream.addTrack(event.track);
    }

    setStatus("playing");
    els.remoteAudio.play().catch((error) => {
      els.warning.textContent = error.message;
    });
  };

  pc.onconnectionstatechange = () => {
    rtcStats.connectionState = pc.connectionState;
    if (pc.connectionState === "connected") setStatus(remoteTrack ? "playing" : "connected");
    if (pc.connectionState === "failed" || pc.connectionState === "disconnected") setStatus(pc.connectionState);
    renderUi();
  };

  pc.oniceconnectionstatechange = () => {
    rtcStats.iceConnectionState = pc.iceConnectionState;
    renderUi();
  };

  pc.onicecandidate = (event) => {
    if (event.candidate) {
      sendJson({
        type: "webrtc-candidate",
        candidate: event.candidate.candidate,
        mid: event.candidate.sdpMid || "audio"
      });
    }
  };

  const signaling = createSocket();
  signaling.onopen = async () => {
    setStatus("signaling");
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    sendJson({
      type: "webrtc-offer",
      sdpType: pc.localDescription.type,
      sdp: pc.localDescription.sdp,
      bitrateBps: activeProfile.bitrateBps,
      bitratePreset: activeProfile.bitratePreset,
      latencyPreset: activeProfile.latencyPreset,
      profileSource: activeProfile.source === "auto" ? "autonegotiation" : "browser_user"
    });
  };
}

async function startAudio(options = {}) {
  if (running || stopping) return;

  if (!autoNegotiation.active) {
    setActiveProfile(profileFromControls(), { updateControls: false });
  }

  resetClientStats();
  running = true;
  stopping = false;
  els.start.textContent = "Stop";
  if (!options.preserveAuto) {
    els.warning.textContent = defaultWarning;
  }
  setStatus("connecting");
  startTimers();
  renderUi();

  try {
    await startWebRtc();
  } catch (error) {
    els.warning.textContent = error.message;
    await stopAudio("error");
  }

  renderUi();
}

async function stopAudio(finalStatus = "stopped", options = {}) {
  if (stopping) return;

  if (!options.preserveAuto) {
    cancelAutoNegotiation("cancelled");
    profileApplyInProgress = false;
  }

  stopping = true;
  running = false;
  els.start.disabled = true;
  setStatus("stopping");
  renderUi();

  sendJson({ type: "webrtc-stop" });

  if (socket) {
    const closingSocket = socket;
    socket = null;
    closingSocket.onopen = null;
    closingSocket.onmessage = null;
    closingSocket.onerror = null;
    closingSocket.onclose = null;
    if (closingSocket.readyState === WebSocket.CONNECTING || closingSocket.readyState === WebSocket.OPEN) {
      closingSocket.close(1000, "stopped");
    }
  }

  if (pc) {
    pc.ontrack = null;
    pc.onicecandidate = null;
    pc.onconnectionstatechange = null;
    pc.oniceconnectionstatechange = null;
    pc.close();
    pc = null;
  }

  remoteStream = null;
  remoteTrack = null;
  els.remoteAudio.pause();
  els.remoteAudio.srcObject = null;

  stopTimers();
  resetClientStats();
  setStatus(finalStatus);
  els.start.textContent = "Connect / Play";
  els.start.disabled = false;
  stopping = false;
  renderUi();
}

function finiteNumberOrNull(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function nonNegativeNumberOrNull(value) {
  const parsed = finiteNumberOrNull(value);
  return parsed !== null && parsed >= 0 ? parsed : null;
}

function selectInboundAudioStats(report) {
  const candidates = [];
  report.forEach((stat) => {
    if (stat.type === "inbound-rtp" && stat.kind === "audio" && !stat.isRemote) {
      candidates.push(stat);
    }
  });

  if (candidates.length === 0) return null;

  candidates.sort((left, right) => {
    const rightPackets = nonNegativeNumberOrNull(right.packetsReceived) || 0;
    const leftPackets = nonNegativeNumberOrNull(left.packetsReceived) || 0;
    return rightPackets - leftPackets;
  });

  return candidates[0];
}

function inboundSnapshot(stat) {
  if (!stat) return null;

  const packetsReceived = nonNegativeNumberOrNull(stat.packetsReceived);
  const packetsLost = finiteNumberOrNull(stat.packetsLost);
  const sameStatsSource = rtcStats.inboundStatsId === stat.id;
  const packetsReceivedIncreasing = sameStatsSource
    && packetsReceived !== null
    && Number.isFinite(rtcStats.packetsReceived)
    && packetsReceived > rtcStats.packetsReceived;

  return {
    id: stat.id,
    bytesReceived: nonNegativeNumberOrNull(stat.bytesReceived),
    packetsReceived,
    packetsLost,
    packetsDiscarded: nonNegativeNumberOrNull(stat.packetsDiscarded),
    packetsReceivedIncreasing,
    jitter: finiteNumberOrNull(stat.jitter),
    concealedSamples: stat.concealedSamples ?? stat.concealedAudio ?? null,
    concealmentEvents: stat.concealmentEvents ?? null,
    audioLevel: finiteNumberOrNull(stat.audioLevel),
    jitterBufferDelay: finiteNumberOrNull(stat.jitterBufferDelay),
    jitterBufferTargetDelay: finiteNumberOrNull(stat.jitterBufferTargetDelay)
  };
}

async function refreshRtcStats() {
  if (!pc) return;

  try {
    const statsPeer = pc;
    const report = await statsPeer.getStats();
    if (statsPeer !== pc) return;

    let codec = null;
    let selectedPair = null;

    report.forEach((stat) => {
      if (stat.type === "codec" && String(stat.mimeType || "").toLowerCase().includes("opus")) {
        codec = stat;
      }

      if (stat.type === "candidate-pair" && (stat.selected || stat.nominated)) {
        selectedPair = stat;
      }
    });

    if (codec) {
      rtcStats.codec = `${codec.mimeType || "audio/opus"} ${codec.clockRate || 48000} Hz`;
    }

    if (selectedPair && typeof selectedPair.currentRoundTripTime === "number") {
      rtcStats.roundTripTimeMs = selectedPair.currentRoundTripTime * 1000;
    }

    const inbound = inboundSnapshot(selectInboundAudioStats(report));
    if (!inbound) {
      updateAutoNegotiation(null).catch((error) => {
        autoNegotiation.active = false;
        autoNegotiation.state = "error";
        autoNegotiation.lastFailure = error.message || "auto negotiation error";
        els.warning.textContent = autoNegotiation.lastFailure;
        renderUi();
      });
      return;
    }

    const now = performance.now();
    if (inbound.bytesReceived !== null
        && rtcStats.lastBytesAt > 0
        && now > rtcStats.lastBytesAt
        && inbound.bytesReceived >= rtcStats.lastBytes) {
      rtcStats.actualBitrateKbps = ((inbound.bytesReceived - rtcStats.lastBytes) * 8) / (now - rtcStats.lastBytesAt);
    }
    rtcStats.lastBytes = inbound.bytesReceived === null ? rtcStats.lastBytes : inbound.bytesReceived;
    rtcStats.lastBytesAt = now;

    rtcStats.inboundStatsId = inbound.id;
    rtcStats.packetsReceivedIncreasing = inbound.packetsReceivedIncreasing;
    rtcStats.packetsReceived = inbound.packetsReceived;
    rtcStats.bytesReceived = inbound.bytesReceived;
    rtcStats.packetsLost = inbound.packetsLost;
    rtcStats.packetsDiscarded = inbound.packetsDiscarded;
    rtcStats.jitterMs = inbound.jitter === null ? 0 : inbound.jitter * 1000;
    rtcStats.concealedSamples = inbound.concealedSamples;
    rtcStats.concealmentEvents = inbound.concealmentEvents;
    rtcStats.audioLevel = inbound.audioLevel;
    rtcStats.jitterBufferDelayMs = inbound.jitterBufferDelay !== null
      ? inbound.jitterBufferDelay * 1000
      : null;
    rtcStats.jitterBufferTargetDelayMs = inbound.jitterBufferTargetDelay !== null
      ? inbound.jitterBufferTargetDelay * 1000
      : null;

    updateAutoNegotiation(inbound).catch((error) => {
      autoNegotiation.active = false;
      autoNegotiation.state = "error";
      autoNegotiation.lastFailure = error.message || "auto negotiation error";
      els.warning.textContent = autoNegotiation.lastFailure;
      renderUi();
    });

    sendJson({
      type: "browser_receiver_stats",
      packetsReceived: rtcStats.packetsReceived,
      bytesReceived: rtcStats.bytesReceived,
      packetsLost: rtcStats.packetsLost,
      packetsDiscarded: rtcStats.packetsDiscarded,
      jitterMs: rtcStats.jitterMs,
      concealedSamples: rtcStats.concealedSamples,
      concealmentEvents: rtcStats.concealmentEvents,
      audioLevel: rtcStats.audioLevel,
      receiverTrackMuted: remoteTrack ? remoteTrack.muted : null,
      receiverTrackReadyState: remoteTrack ? remoteTrack.readyState : "none"
    });
  } catch {
    // Stats availability varies by browser and connection state.
  }
}

function renderUi() {
  els.connection.textContent = client.status;
  els.transport.textContent = "WebRTC Opus";
  els.codec.textContent = rtcStats.codec;
  els.bitrate.textContent = activeProfile.bitratePreset;

  els.rtcConnectionState.textContent = pc ? pc.connectionState : rtcStats.connectionState;
  els.iceConnectionState.textContent = pc ? pc.iceConnectionState : rtcStats.iceConnectionState;
  els.actualBitrate.textContent = `${rtcStats.actualBitrateKbps.toFixed(1)} kb/s`;
  els.packetsReceived.textContent = formatPacketCount(rtcStats.packetsReceived);
  els.packetsLost.textContent = formatPacketCount(rtcStats.packetsLost);
  els.jitter.textContent = `${rtcStats.jitterMs.toFixed(1)} ms`;
  els.roundTripTime.textContent = rtcStats.roundTripTimeMs === null ? "-" : `${rtcStats.roundTripTimeMs.toFixed(1)} ms`;
  els.concealedSamples.textContent = rtcStats.concealedSamples === null ? "-" : String(rtcStats.concealedSamples);
  els.jitterBufferDelay.textContent = rtcStats.jitterBufferDelayMs === null ? "-" : `${rtcStats.jitterBufferDelayMs.toFixed(1)} ms`;
  els.jitterBufferTargetDelay.textContent = rtcStats.jitterBufferTargetDelayMs === null ? "-" : `${rtcStats.jitterBufferTargetDelayMs.toFixed(1)} ms`;
  els.remoteTrackState.textContent = remoteTrack
    ? `${remoteTrack.readyState} ${remoteTrack.muted ? "muted" : "unmuted"}`
      + ` bytes ${formatPacketCount(rtcStats.bytesReceived)}`
      + ` level ${rtcStats.audioLevel === null ? "-" : rtcStats.audioLevel.toFixed(3)}`
    : "none";
  els.latencyPreset.textContent = `${activeProfile.latencyPreset} (${activeProfile.latencyTarget})`;

  els.senderQueueFill.textContent = `${server.senderQueueFillMs.toFixed(1)} ms`;
  els.senderDroppedFrames.textContent = String(server.senderDroppedFrames);
  els.encoderOverloads.textContent = String(server.encoderOverloads);
  els.webrtcOpenTracks.textContent = String(server.webrtcOpenTracks);
  els.signalingState.textContent = socketStateName();
  els.rtpSent.textContent = String(server.rtpSent);
  els.rtpFailures.textContent = String(server.rtpFailures);
  els.rtpAttempts.textContent = String(server.rtpAttempts);

  els.autoActive.textContent = autoNegotiation.active ? "active" : "inactive";
  els.autoMode.textContent = autoModeLabel(autoNegotiation.mode);
  els.autoProfile.textContent = autoNegotiation.active
    || autoNegotiation.state === "complete"
    || autoNegotiation.state === "failed all profiles"
    ? profileLabel()
    : "-";
  els.autoPhase.textContent = autoNegotiation.state;
  els.autoElapsed.textContent = `${(autoNegotiation.elapsedMs / 1000).toFixed(1)} s`;
  els.autoStableTime.textContent = `${(autoNegotiation.stableMs / 1000).toFixed(1)} s`;
  els.autoBaselinePacketsLost.textContent = formatPacketCount(autoNegotiation.baselinePacketsLost);
  els.autoCurrentPacketsLost.textContent = formatPacketCount(autoNegotiation.currentPacketsLost);
  els.autoLossDelta.textContent = String(autoNegotiation.lossDelta);
  els.autoProfileIndex.textContent = autoNegotiation.profilesTested > 0
    ? `${autoNegotiation.profilesTested}/${autoMaxProfiles}`
    : `0/${autoMaxProfiles}`;
  els.activeEncoderBitrate.textContent = `${formatBitrateKbps(server.opusBitrateBps)}`
    + ` rev ${server.stateRevision} ${server.stateOrigin}`;
  els.activeOpusFrame.textContent = `${server.opusFrameDurationMs} ms`;
  els.autoProfileChanges.textContent = String(autoNegotiation.profileChanges);
  els.autoFifoUnderruns.textContent = String(server.fifoUnderruns);
  els.autoFinalProfile.textContent = autoNegotiation.finalProfile;
  els.autoLastFailure.textContent = autoNegotiation.lastFailure === "-"
    ? `${autoNegotiation.readiness}; native ${server.lastAdaptationReason}; rx ${lastStateUpdateReceivedTimestamp}; suppressed ${uiSyncSuppressedEventCount}`
    : autoNegotiation.lastFailure;

  els.bitrateSelect.disabled = autoNegotiation.active || profileApplyInProgress;
  els.latencySelect.disabled = autoNegotiation.active || profileApplyInProgress;
  els.autoModeSelect.disabled = autoNegotiation.active;
  els.autoButton.textContent = autoNegotiation.active ? "Cancel Auto" : "Auto Negotiate";
}

async function reconnectForManualProfileChange() {
  if (!running || stopping || autoNegotiation.active) return;

  markProfileApplyPending();
  els.warning.textContent = `Applying profile: ${activeProfileLabel()}`;
  renderUi();

  await stopAudio("profile reconnect", { preserveAuto: true });
  await startAudio({ preserveAuto: true });
}

function handleManualProfileChange() {
  if (suppressControlEvents > 0) {
    uiSyncSuppressedEventCount += 1;
    return;
  }

  if (autoNegotiation.active) return;

  profileUserSelected = true;
  setActiveProfile(profileFromControls(), { updateControls: false });

  if (running && !stopping) {
    reconnectForManualProfileChange().catch((error) => {
      profileApplyInProgress = false;
      els.warning.textContent = error.message || "Profile reconnect failed";
      renderUi();
    });
  } else {
    renderUi();
  }
}

els.bitrateSelect.addEventListener("change", handleManualProfileChange);
els.latencySelect.addEventListener("change", handleManualProfileChange);

els.start.addEventListener("click", () => {
  if (running) {
    stopAudio();
  } else {
    startAudio();
  }
});

els.nerd.addEventListener("click", () => {
  const expanded = els.nerd.getAttribute("aria-expanded") === "true";
  els.nerd.setAttribute("aria-expanded", expanded ? "false" : "true");
  els.nerdPanel.hidden = expanded;
  renderUi();
});

els.autoButton.addEventListener("click", () => {
  startAutoNegotiation().catch((error) => {
    autoNegotiation.active = false;
    autoNegotiation.state = "error";
    autoNegotiation.lastFailure = error.message || "auto negotiation error";
    els.warning.textContent = autoNegotiation.lastFailure;
    renderUi();
  });
});

refreshServerInfo();
startServerTimer();
renderUi();
