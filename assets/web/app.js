"use strict";

const els = {
  start: document.getElementById("startButton"),
  nerd: document.getElementById("nerdButton"),
  nerdPanel: document.getElementById("nerdPanel"),
  engineSelect: document.getElementById("engineSelect"),
  bitrateSelect: document.getElementById("bitrateSelect"),
  bitrateReadout: document.getElementById("bitrateReadout"),
  latencySelect: document.getElementById("latencySelect"),
  autoModeSelect: document.getElementById("autoModeSelect"),
  autoButton: document.getElementById("autoButton"),
  remoteAudio: document.getElementById("remoteAudio"),
  connection: document.getElementById("connection"),
  transport: document.getElementById("transport"),
  codec: document.getElementById("codec"),
  bitrate: document.getElementById("bitrate"),
  lastReason: document.getElementById("lastReason"),
  receiverProtocol: document.getElementById("receiverProtocol"),
  secureContext: document.getElementById("secureContext"),
  audioWorkletAvailable: document.getElementById("audioWorkletAvailable"),
  audioContextSampleRate: document.getElementById("audioContextSampleRate"),
  audioContextBaseLatency: document.getElementById("audioContextBaseLatency"),
  audioContextOutputLatency: document.getElementById("audioContextOutputLatency"),
  signalingScheme: document.getElementById("signalingScheme"),
  receiverScheme: document.getElementById("receiverScheme"),
  certificateMode: document.getElementById("certificateMode"),
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
  pcmEngine: document.getElementById("pcmEngine"),
  pcmDataChannelState: document.getElementById("pcmDataChannelState"),
  pcmQueuedBytes: document.getElementById("pcmQueuedBytes"),
  pcmWorkletState: document.getElementById("pcmWorkletState"),
  pcmSecureContext: document.getElementById("pcmSecureContext"),
  pcmSabState: document.getElementById("pcmSabState"),
  pcmBufferMs: document.getElementById("pcmBufferMs"),
  pcmTargetBufferMs: document.getElementById("pcmTargetBufferMs"),
  pcmPackets: document.getElementById("pcmPackets"),
  pcmStreamInfo: document.getElementById("pcmStreamInfo"),
  pcmPacketDuration: document.getElementById("pcmPacketDuration"),
  pcmDrops: document.getElementById("pcmDrops"),
  pcmPacketGaps: document.getElementById("pcmPacketGaps"),
  pcmAckAge: document.getElementById("pcmAckAge"),
  pcmLastReason: document.getElementById("pcmLastReason"),
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
const autoSuccessMs = 2000;
const autoStatsUnavailableMs = 10000;
const bitrateProfiles = [510000, 320000, 256000, 192000, 128000];
const latencyProfiles = ["ultra", "low", "medium", "safe", "very-safe"];
const pcmEngineOrder = ["pcm32f", "pcm24", "pcm16", "opus"];
const pcmPacketFrames = 480;
const pcmSampleRate = 48000;
const pcmRingCapacityFrames = pcmSampleRate;
const autoMaxProfiles = pcmEngineOrder.length * latencyProfiles.length;

let socket = null;
let pc = null;
let remoteStream = null;
let remoteTrack = null;
let currentEngine = "opus";
let pcmDataChannel = null;
let pcmAudioContext = null;
let pcmWorkletNode = null;
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
  selectedTransport: "WebRTC Opus",
  serverScheme: window.location.protocol === "https:" ? "https" : "http",
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
  rtpFailures: 0,
  httpsEnabled: false,
  selfSignedCertificateEnabled: false,
  pcmBitsPerSample: 0,
  pcmPacketFrames: 480,
  pcmTargetBufferMs: 70,
  pcmOpenChannels: 0,
  pcmReceiverReadyCount: 0,
  pcmPacketsSent: 0,
  pcmBytesSent: 0,
  pcmSendFailures: 0,
  pcmDroppedBeforeSend: 0,
  pcmDataChannelBufferedBytes: 0,
  pcmReceiverAckAgeMs: -1,
  pcmReceiverBufferMs: 0,
  pcmReceiverUnderflows: 0,
  pcmReceiverOverflows: 0,
  pcmReceiverMissingPackets: 0,
  pcmReceiverLatePackets: 0,
  pcmAudioContextSampleRate: 0,
  pcmAudioContextBaseLatencyMs: -1,
  pcmAudioContextOutputLatencyMs: -1,
  pcmReceiverLastError: "-"
};

const pcm = {
  state: "IDLE",
  dataChannelState: "closed",
  audioWorkletAvailable: false,
  audioWorkletModuleLoaded: false,
  audioWorkletNodeCreated: false,
  audioWorkletProcessorReady: false,
  ringBufferReady: false,
  secureContextOk: window.isSecureContext,
  crossOriginIsolated: !!window.crossOriginIsolated,
  sabAvailable: typeof SharedArrayBuffer !== "undefined",
  usingSab: false,
  header: null,
  ringLeft: null,
  ringRight: null,
  ringCapacityFrames: 0,
  targetBufferMs: 70,
  sampleRate: 48000,
  packetFrames: 480,
  packetDurationMs: 10,
  streamId: null,
  lastAcceptedSequence: null,
  packetsReceived: 0,
  bytesReceived: 0,
  droppedInvalid: 0,
  droppedDuplicate: 0,
  droppedStale: 0,
  missingPackets: 0,
  latePackets: 0,
  overflows: 0,
  underflows: 0,
  queuedBytes: 0,
  audioContextSampleRate: 0,
  audioContextBaseLatencyMs: -1,
  audioContextOutputLatencyMs: -1,
  bufferFrames: 0,
  lastPacketAt: 0,
  lastAckAt: 0,
  lastError: "-",
  lastReason: "-"
};

let activeProfile = makeProfile(320000, "medium", "initial");
let profileApplyInProgress = false;
let profileApplyDeadlineMs = 0;
let profileUserSelected = false;

const autoNegotiation = {
  active: false,
  mode: "quality",
  candidates: [],
  candidateIndex: 0,
  currentCandidate: null,
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
  baselineUnderflows: 0,
  baselineSenderDrops: 0,
  baselineMissingPackets: 0,
  baselineLatePackets: 0,
  baselineOverflows: 0,
  profileChanges: 0,
  profilesTested: 0,
  finalProfile: "-",
  lastFailure: "-",
  readiness: "not started",
  transitioning: false
};

function setStatus(status) {
  client.status = status;
  updateMediaSession(status === "playing");
}

function updateMediaSession(active) {
  if (!("mediaSession" in navigator)) return;

  try {
    if (active && "MediaMetadata" in window) {
      navigator.mediaSession.metadata = new MediaMetadata({
        title: "PGStream",
        artist: "Pigeon Stream",
        album: "Live DAW audio"
      });
    }
    navigator.mediaSession.playbackState = active ? "playing" : "paused";
  } catch {
    // Media Session support varies by browser/device.
  }
}

function resetPcmReceiver() {
  pcm.state = "IDLE";
  pcm.dataChannelState = "closed";
  pcm.audioWorkletAvailable = false;
  pcm.audioWorkletModuleLoaded = false;
  pcm.audioWorkletNodeCreated = false;
  pcm.audioWorkletProcessorReady = false;
  pcm.ringBufferReady = false;
  pcm.secureContextOk = window.isSecureContext;
  pcm.crossOriginIsolated = !!window.crossOriginIsolated;
  pcm.sabAvailable = typeof SharedArrayBuffer !== "undefined";
  pcm.usingSab = false;
  pcm.header = null;
  pcm.ringLeft = null;
  pcm.ringRight = null;
  pcm.ringCapacityFrames = 0;
  pcm.sampleRate = 48000;
  pcm.packetFrames = 480;
  pcm.packetDurationMs = 10;
  pcm.streamId = null;
  pcm.lastAcceptedSequence = null;
  pcm.packetsReceived = 0;
  pcm.bytesReceived = 0;
  pcm.droppedInvalid = 0;
  pcm.droppedDuplicate = 0;
  pcm.droppedStale = 0;
  pcm.missingPackets = 0;
  pcm.latePackets = 0;
  pcm.overflows = 0;
  pcm.underflows = 0;
  pcm.queuedBytes = 0;
  pcm.audioContextSampleRate = 0;
  pcm.audioContextBaseLatencyMs = -1;
  pcm.audioContextOutputLatencyMs = -1;
  pcm.bufferFrames = 0;
  pcm.lastPacketAt = 0;
  pcm.lastAckAt = 0;
  pcm.lastError = "-";
  pcm.lastReason = "-";
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
  resetPcmReceiver();
}

function bitrateLabel(bps) {
  if (bps === 128000) return "128 kb/s Good Preview";
  if (bps === 192000) return "192 kb/s Very Good";
  if (bps === 256000) return "256 kb/s Studio Preview";
  if (bps === 510000) return "510 kb/s Experimental Max";
  return "320 kb/s High Quality / Recommended";
}

function latencyLabel(value) {
  if (value === "ultra") return "Ultra Low";
  if (value === "low") return "Low";
  if (value === "medium") return "Medium";
  if (value === "safe") return "Safe";
  if (value === "very-safe") return "Very Safe";
  return "Medium";
}

function latencyTarget(value) {
  if (value === "ultra") return "20 ms PCM target fill";
  if (value === "low") return "40 ms PCM target fill";
  if (value === "medium") return "70 ms PCM target fill";
  if (value === "safe") return "120 ms PCM target fill";
  if (value === "very-safe") return "180 ms PCM target fill";
  return "70 ms PCM target fill";
}

function pcmTargetFillMsForLatency(value) {
  if (value === "ultra") return 20;
  if (value === "low") return 40;
  if (value === "safe") return 120;
  if (value === "very-safe") return 180;
  return 70;
}

function latencyFrameMs(value) {
  if (value === "ultra") return 10;
  if (value === "low") return 10;
  return 20;
}

function selectedEngine() {
  return els.engineSelect ? els.engineSelect.value : "opus";
}

function isPcmEngine(engine = currentEngine) {
  return engine === "pcm16" || engine === "pcm24" || engine === "pcm32f";
}

function transportModeForEngine(engine = currentEngine) {
  if (engine === "pcm16") return "PCM16 DataChannel";
  if (engine === "pcm24") return "PCM24 DataChannel";
  if (engine === "pcm32f") return "PCM32F DataChannel";
  if (engine === "auto") return "Auto";
  return "WebRTC Opus";
}

function engineValueFromTransport(text) {
  const value = String(text || "").toLowerCase();
  if (value.includes("pcm16")) return "pcm16";
  if (value.includes("pcm24")) return "pcm24";
  if (value.includes("pcm32") || value.includes("float")) return "pcm32f";
  if (value.includes("auto")) return "auto";
  return "opus";
}

function setEngineControl(engine) {
  const normalized = ["opus", "pcm16", "pcm24", "pcm32f", "auto"].includes(engine) ? engine : "opus";
  suppressControlEvents += 1;
  try {
    els.engineSelect.value = normalized;
    currentEngine = normalized;
  } finally {
    suppressControlEvents -= 1;
  }
}

function pcmFormatCode(engine = currentEngine) {
  if (engine === "pcm32f") return 3;
  return engine === "pcm16" ? 1 : 2;
}

function pcmFormatLabel(engine = currentEngine) {
  if (engine === "pcm16") return "PCM16 48 kHz stereo, approx. 1536 kb/s";
  if (engine === "pcm24") return "PCM24 48 kHz stereo, approx. 2304 kb/s";
  if (engine === "pcm32f") return "PCM32F 48 kHz stereo, approx. 3072 kb/s";
  return "opus/48000/2";
}

function resolveEngineForStart() {
  const selected = selectedEngine();
  if (selected === "auto") {
    return server.httpsEnabled && window.isSecureContext ? "pcm32f" : "opus";
  }

  if (isPcmEngine(selected) && (!server.httpsEnabled || !window.isSecureContext)) {
    throw new Error("Lossless PCM requires self-signed HTTPS. Enable it in Settings to use PCM16/PCM24/PCM32F.");
  }

  return selected;
}

function normalizeBitrateBps(value) {
  const parsed = Number(value);
  return bitrateProfiles.includes(parsed) ? parsed : 320000;
}

function normalizeLatencyValue(value) {
  const text = String(value || "").toLowerCase();
  if (text === "balanced") return "medium";
  return latencyProfiles.includes(text) ? text : "medium";
}

function latencyValueFromPreset(preset) {
  const lower = String(preset || "").toLowerCase();
  if (lower.includes("ultra")) return "ultra";
  if (lower.includes("low")) return "low";
  if (lower.includes("medium") || lower.includes("balanced")) return "medium";
  if (lower.includes("very")) return "very-safe";
  if (lower.includes("safe")) return "safe";
  return "medium";
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
  pcm.targetBufferMs = pcmTargetFillMsForLatency(activeProfile.latencyValue);

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
  if (autoNegotiation.currentCandidate) {
    return `${transportModeForEngine(autoNegotiation.currentCandidate.engine).replace(" DataChannel", "")} / ${latencyLabel(autoNegotiation.currentCandidate.latencyValue)}`;
  }
  return activeProfileLabel(makeProfile(bitrateProfiles[bitrateIndex], latencyProfiles[latencyIndex], "auto"));
}

function profileFromAutoIndices() {
  if (autoNegotiation.currentCandidate) {
    return makeProfile(
      autoNegotiation.currentCandidate.bitrateBps,
      autoNegotiation.currentCandidate.latencyValue,
      "auto"
    );
  }

  return makeProfile(
    bitrateProfiles[autoNegotiation.bitrateIndex],
    latencyProfiles[autoNegotiation.latencyIndex],
    "auto"
  );
}

function applyAutoProfile() {
  if (autoNegotiation.currentCandidate) {
    setEngineControl(autoNegotiation.currentCandidate.engine);
  }
  setActiveProfile(profileFromAutoIndices());
}

function makeAutoCandidate(engine, latencyValue) {
  return {
    engine,
    latencyValue,
    bitrateBps: engine === "opus" ? activeProfile.bitrateBps : 320000
  };
}

function buildAutoCandidates(mode) {
  const candidates = [];
  const engines = server.httpsEnabled && window.isSecureContext
    ? [...pcmEngineOrder]
    : ["opus"];
  if (mode === "quality") {
    for (const engine of engines) {
      for (const latency of latencyProfiles) {
        candidates.push(makeAutoCandidate(engine, latency));
      }
    }
    return candidates;
  }

  for (const latency of latencyProfiles) {
    for (const engine of engines) {
      candidates.push(makeAutoCandidate(engine, latency));
    }
  }
  return candidates;
}

function resetAutoMeasurement(state = "connecting") {
  const now = performance.now();
  autoNegotiation.state = state;
  autoNegotiation.inboundStatsId = null;
  autoNegotiation.baselinePacketsLost = null;
  autoNegotiation.baselineUnderflows = pcm.underflows;
  autoNegotiation.baselineSenderDrops = server.pcmDroppedBeforeSend;
  autoNegotiation.baselineMissingPackets = pcm.missingPackets;
  autoNegotiation.baselineLatePackets = pcm.latePackets;
  autoNegotiation.baselineOverflows = pcm.overflows;
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
  autoNegotiation.candidateIndex += 1;
  if (autoNegotiation.candidateIndex >= autoNegotiation.candidates.length) {
    return false;
  }
  autoNegotiation.currentCandidate = autoNegotiation.candidates[autoNegotiation.candidateIndex];
  return true;
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

  if (isPcmEngine()) {
    if (!pcmDataChannel || pcmDataChannel.readyState !== "open") {
      return `waiting for PCM DataChannel (${pcmDataChannel ? pcmDataChannel.readyState : "none"})`;
    }

    if (!pcm.audioWorkletProcessorReady || !pcm.ringBufferReady) {
      return "waiting for PCM AudioWorklet";
    }

    if (pcmBufferMs() < Math.max(5, pcm.targetBufferMs * 0.5)) {
      return "waiting for PCM preroll buffer";
    }

    return "";
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
  autoNegotiation.candidates = buildAutoCandidates(autoNegotiation.mode);
  autoNegotiation.candidateIndex = 0;
  autoNegotiation.currentCandidate = autoNegotiation.candidates[0] || makeAutoCandidate("opus", "safe");
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
  sendStreamControlUpdate("auto-complete");
  setAutoWarning(`Auto negotiation complete: ${autoNegotiation.finalProfile}`);
  renderUi();
}

async function failAutoNegotiation(delta) {
  autoNegotiation.state = "profile failed";
  autoNegotiation.lastFailure = typeof delta === "number"
    ? `${profileLabel()} had ${delta} PCM/WebRTC failure${delta === 1 ? "" : "s"} during measurement`
    : `${profileLabel()} failed: ${delta}`;
  renderUi();

  if (!nextAutoProfile()) {
    autoNegotiation.currentCandidate = makeAutoCandidate("opus", "very-safe");
    applyAutoProfile();
    autoNegotiation.active = false;
    autoNegotiation.state = "failed all profiles";
    autoNegotiation.finalProfile = profileLabel();
    sendStreamControlUpdate("auto-fallback");
    setAutoWarning("Auto negotiation could not find a zero-loss profile. Using safest available settings.");
    if (running && !stopping) {
      await reconnectForManualProfileChange();
    }
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

  const pcmFailureDelta = isPcmEngine()
    ? (pcm.underflows - autoNegotiation.baselineUnderflows)
      + (server.pcmDroppedBeforeSend - autoNegotiation.baselineSenderDrops)
      + (pcm.missingPackets - autoNegotiation.baselineMissingPackets)
      + (pcm.latePackets - autoNegotiation.baselineLatePackets)
      + (pcm.overflows - autoNegotiation.baselineOverflows)
    : 0;

  if (isPcmEngine() && server.pcmDataChannelBufferedBytes >= 256 * 1024) {
    await failAutoNegotiation("DataChannel queue did not recover");
    return;
  }

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
      autoNegotiation.baselineUnderflows = pcm.underflows;
      autoNegotiation.baselineSenderDrops = server.pcmDroppedBeforeSend;
      autoNegotiation.baselineMissingPackets = pcm.missingPackets;
      autoNegotiation.baselineLatePackets = pcm.latePackets;
      autoNegotiation.baselineOverflows = pcm.overflows;
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
  const delta = currentPacketsLost - autoNegotiation.baselinePacketsLost + Math.max(0, pcmFailureDelta);

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
  if (info.selectedTransport) server.selectedTransport = info.selectedTransport;
  if (info.serverScheme) server.serverScheme = info.serverScheme;
  if (typeof info.httpsEnabled === "boolean") server.httpsEnabled = info.httpsEnabled;
  if (typeof info.selfSignedCertificateEnabled === "boolean") {
    server.selfSignedCertificateEnabled = info.selfSignedCertificateEnabled;
  }
  if (Number.isFinite(Number(info.pcmBitsPerSample))) server.pcmBitsPerSample = Number(info.pcmBitsPerSample);
  if (Number.isFinite(Number(info.pcmPacketFrames))) server.pcmPacketFrames = Number(info.pcmPacketFrames);
  if (Number.isFinite(Number(info.pcmTargetBufferMs))) {
    server.pcmTargetBufferMs = Number(info.pcmTargetBufferMs);
    pcm.targetBufferMs = server.pcmTargetBufferMs;
  }
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
  if (info.autonegotiationMode) {
    const syncedMode = ["quality", "latency"].includes(info.autonegotiationMode) ? info.autonegotiationMode : "quality";
    autoNegotiation.mode = syncedMode;
    if (!autoNegotiation.active && els.autoModeSelect.value !== syncedMode) {
      els.autoModeSelect.value = syncedMode;
    }
  }

  const selectedEngineFromServer = engineValueFromTransport(info.selectedTransport || info.transport);
  if (!running && !autoNegotiation.active && !profileApplyInProgress && !profileUserSelected) {
    setEngineControl(server.httpsEnabled ? selectedEngineFromServer : "opus");
  }

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
    server.pcmOpenChannels = Number(info.server.pcmOpenChannels || 0);
    server.pcmReceiverReadyCount = Number(info.server.pcmReceiverReadyCount || 0);
    server.pcmPacketsSent = Number(info.server.pcmPacketsSent || 0);
    server.pcmBytesSent = Number(info.server.pcmBytesSent || 0);
    server.pcmSendFailures = Number(info.server.pcmSendFailures || 0);
    server.pcmDroppedBeforeSend = Number(info.server.pcmDroppedBeforeSend || 0);
    server.pcmDataChannelBufferedBytes = Number(info.server.pcmDataChannelBufferedBytes || 0);
    server.pcmReceiverAckAgeMs = Number(info.server.pcmReceiverAckAgeMs ?? -1);
    server.pcmReceiverBufferMs = Number(info.server.pcmReceiverBufferMs || 0);
    server.pcmReceiverUnderflows = Number(info.server.pcmReceiverUnderflows || 0);
    server.pcmReceiverOverflows = Number(info.server.pcmReceiverOverflows || 0);
    server.pcmReceiverMissingPackets = Number(info.server.pcmReceiverMissingPackets || 0);
    server.pcmReceiverLatePackets = Number(info.server.pcmReceiverLatePackets || 0);
    server.pcmAudioContextSampleRate = Number(info.server.pcmAudioContextSampleRate || 0);
    server.pcmAudioContextBaseLatencyMs = Number(info.server.pcmAudioContextBaseLatencyMs ?? -1);
    server.pcmAudioContextOutputLatencyMs = Number(info.server.pcmAudioContextOutputLatencyMs ?? -1);
    server.pcmReceiverLastError = info.server.pcmReceiverLastError || "-";

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
  server.transport = message.transportMode || server.transport;
  server.selectedTransport = message.selectedTransportMode || server.selectedTransport;
  server.serverScheme = message.serverScheme || server.serverScheme;
  if (typeof message.httpsEnabled === "boolean") server.httpsEnabled = message.httpsEnabled;
  if (message.autonegotiationMode && !autoNegotiation.active) {
    autoNegotiation.mode = ["quality", "latency"].includes(message.autonegotiationMode) ? message.autonegotiationMode : "quality";
    els.autoModeSelect.value = autoNegotiation.mode;
  }
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
  if (!profileApplyInProgress || message.origin !== "browser_user") {
    setEngineControl(server.httpsEnabled
      ? engineValueFromTransport(server.selectedTransport || server.transport)
      : "opus");
  }
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

function sendStreamControlUpdate(reason = "browser_control") {
  sendJson({
    type: "stream_control_request",
    requestId: `${reason}-${Date.now()}`,
    bitrateBps: activeProfile.bitrateBps,
    bitratePreset: activeProfile.bitratePreset,
    latencyPreset: activeProfile.latencyPreset,
    transportMode: transportModeForEngine(selectedEngine()),
    autoMode: els.autoModeSelect.value,
    autonegotiationState: autoNegotiation.active ? "running" : "selected"
  });
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
  const wsScheme = window.location.protocol === "https:" ? "wss" : "ws";
  const wsUrl = `${wsScheme}://${window.location.host}/ws`;
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

function pcmBufferFrames() {
  if (pcm.header) {
    return Math.max(0, Atomics.load(pcm.header, 0) - Atomics.load(pcm.header, 1));
  }
  return pcm.bufferFrames;
}

function pcmBufferMs() {
  return (pcmBufferFrames() / pcmSampleRate) * 1000;
}

function targetFillFrames() {
  return Math.max(128, Math.round((pcm.targetBufferMs / 1000) * pcmSampleRate));
}

function resetPcmStream(streamId) {
  pcm.streamId = streamId;
  pcm.lastAcceptedSequence = null;
  pcm.missingPackets = 0;
  pcm.latePackets = 0;
  pcm.overflows = 0;
  pcm.bufferFrames = 0;
  if (pcm.header) {
    Atomics.store(pcm.header, 0, 0);
    Atomics.store(pcm.header, 1, 0);
    Atomics.store(pcm.header, 3, 0);
  }
  if (pcmWorkletNode) {
    pcmWorkletNode.port.postMessage({ type: "reset" });
  }
  pcm.state = "PREROLLING";
}

function trimPcmRingToTarget(write, read, capacity) {
  let available = Math.max(0, write - read);
  const maxFill = Math.min(capacity, Math.max(targetFillFrames() * 2, targetFillFrames() + pcmPacketFrames));
  if (available <= maxFill) return read;

  const dropFrames = available - maxFill;
  read += dropFrames;
  Atomics.store(pcm.header, 1, read);
  pcm.overflows += 1;
  return read;
}

function writePcmSamples(left, right, frames) {
  if (pcm.header && pcm.ringLeft && pcm.ringRight) {
    let write = Atomics.load(pcm.header, 0);
    let read = Atomics.load(pcm.header, 1);
    const capacity = Atomics.load(pcm.header, 2);
    let available = Math.max(0, write - read);
    const needed = Math.floor(frames);

    if (needed > capacity) {
      pcm.overflows += 1;
      return false;
    }

    read = trimPcmRingToTarget(write, read, capacity);
    available = Math.max(0, write - read);

    while (capacity - available < needed) {
      read += Math.min(needed, available);
      Atomics.store(pcm.header, 1, read);
      available = Math.max(0, write - read);
      pcm.overflows += 1;
    }

    for (let i = 0; i < needed; i += 1) {
      const frame = (write + i) % capacity;
      pcm.ringLeft[frame] = left[i];
      pcm.ringRight[frame] = right[i];
    }
    Atomics.store(pcm.header, 0, write + needed);
    pcm.bufferFrames = pcmBufferFrames();
    return true;
  }

  return false;
}

function maybeStartPcmPlayback() {
  if ((pcm.state === "PREROLLING" || pcm.state === "WORKLET_READY" || pcm.state === "UNDERFLOW")
      && pcmBufferFrames() >= targetFillFrames()
      && pcmWorkletNode
      && pcmAudioContext
      && pcmAudioContext.state === "running") {
    pcm.state = "RUNNING";
    pcmWorkletNode.port.postMessage({ type: "set-running", running: true });
    setStatus("playing");
  }
}

function readU32(view, offset) {
  return view.getUint32(offset, true);
}

function readU64PartsAsNumber(low, high) {
  const lowBits = BigInt(low);
  const highBits = BigInt(high);
  const value = (highBits << 32n) | lowBits;
  return Number(value);
}

function decodePcmPacket(buffer) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < 40) {
    pcm.droppedInvalid += 1;
    pcm.lastError = "invalid PCM packet container";
    return null;
  }

  const view = new DataView(buffer);
  if (view.getUint8(0) !== 0x50 || view.getUint8(1) !== 0x47 || view.getUint8(2) !== 0x50 || view.getUint8(3) !== 0x43) {
    pcm.droppedInvalid += 1;
    pcm.lastError = "bad PCM magic";
    return null;
  }

  const version = view.getUint8(4);
  const format = view.getUint8(5);
  const channels = view.getUint8(6);
  const headerBytes = view.getUint8(7);
  const sampleRate = readU32(view, 8);
  const sequence = readU32(view, 12);
  const sampleTimeLow = readU32(view, 16);
  const sampleTimeHigh = readU32(view, 20);
  const frames = readU32(view, 24);
  const payloadBytes = readU32(view, 28);
  const streamId = readU32(view, 32);
  const flags = readU32(view, 36);
  const bytesPerSample = format === 1 ? 2 : (format === 2 ? 3 : 4);
  const expectedPayloadBytes = frames * channels * bytesPerSample;

  if (version !== 1 || format !== pcmFormatCode() || sampleRate !== 48000 || channels !== 2
      || headerBytes !== 40 || frames !== pcmPacketFrames
      || payloadBytes !== expectedPayloadBytes
      || buffer.byteLength !== headerBytes + payloadBytes) {
    pcm.droppedInvalid += 1;
    pcm.lastError = "PCM packet validation failed";
    return null;
  }

  if (pcm.streamId !== null && streamId < pcm.streamId) {
    pcm.droppedStale += 1;
    return null;
  }

  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  let src = headerBytes;
  for (let i = 0; i < frames; i += 1) {
    if (format === 1) {
      left[i] = Math.max(-1, Math.min(1, view.getInt16(src, true) / 32768));
      src += 2;
      right[i] = Math.max(-1, Math.min(1, view.getInt16(src, true) / 32768));
      src += 2;
    } else if (format === 2) {
      let leftValue = view.getUint8(src) | (view.getUint8(src + 1) << 8) | (view.getUint8(src + 2) << 16);
      if (leftValue & 0x800000) leftValue |= 0xff000000;
      left[i] = Math.max(-1, Math.min(1, leftValue / 8388608));
      src += 3;
      let rightValue = view.getUint8(src) | (view.getUint8(src + 1) << 8) | (view.getUint8(src + 2) << 16);
      if (rightValue & 0x800000) rightValue |= 0xff000000;
      right[i] = Math.max(-1, Math.min(1, rightValue / 8388608));
      src += 3;
    } else {
      left[i] = view.getFloat32(src, true);
      src += 4;
      right[i] = view.getFloat32(src, true);
      src += 4;
    }
  }

  return {
    streamId,
    sequence,
    sampleTime: readU64PartsAsNumber(sampleTimeLow, sampleTimeHigh),
    frames,
    left,
    right,
    flags,
    bytes: buffer.byteLength
  };
}

function handlePcmData(buffer) {
  const packet = decodePcmPacket(buffer);
  if (!packet) return;

  if (pcm.streamId === null || packet.streamId !== pcm.streamId) {
    resetPcmStream(packet.streamId);
  }

  if (pcm.lastAcceptedSequence !== null && packet.sequence <= pcm.lastAcceptedSequence) {
    pcm.latePackets += 1;
    return;
  }

  if (pcm.lastAcceptedSequence !== null && packet.sequence > pcm.lastAcceptedSequence + 1) {
    pcm.missingPackets += packet.sequence - pcm.lastAcceptedSequence - 1;
  }

  if (writePcmSamples(packet.left, packet.right, packet.frames)) {
    pcm.lastAcceptedSequence = packet.sequence;
    if ((packet.flags & (1 << 3)) !== 0) {
      pcm.lastReason = "sender dropped old audio before this packet";
    } else if ((packet.flags & (1 << 2)) !== 0) {
      pcm.lastReason = "sender discontinuity";
    }
    pcm.sampleRate = pcmSampleRate;
    pcm.packetFrames = packet.frames;
    pcm.packetDurationMs = (packet.frames / pcmSampleRate) * 1000;
    pcm.packetsReceived += 1;
    pcm.bytesReceived += packet.bytes;
    pcm.lastPacketAt = performance.now();
    maybeStartPcmPlayback();
  }
}

function sendPcmReceiverStats() {
  if (!isPcmEngine()) return;

  pcm.lastAckAt = performance.now();
  pcm.queuedBytes = pcmDataChannel ? Number(pcmDataChannel.bufferedAmount || 0) : 0;
  sendJson({
    type: "browser_receiver_stats",
    dataChannelOpen: !!pcmDataChannel && pcmDataChannel.readyState === "open",
    audioContextState: pcmAudioContext ? pcmAudioContext.state : "none",
    audioWorkletAvailable: pcm.audioWorkletAvailable,
    audioWorkletModuleLoaded: pcm.audioWorkletModuleLoaded,
    audioWorkletNodeCreated: pcm.audioWorkletNodeCreated,
    audioWorkletProcessorReady: pcm.audioWorkletProcessorReady,
    ringBufferReady: pcm.ringBufferReady,
    secureContextOk: pcm.secureContextOk,
    currentEngine: transportModeForEngine(),
    streamId: pcm.streamId || 0,
    bufferMs: pcmBufferMs(),
    underflows: pcm.underflows,
    overflows: pcm.overflows,
    missingPackets: pcm.missingPackets,
    latePackets: pcm.latePackets,
    queuedBytes: pcm.queuedBytes,
    audioContextSampleRate: pcm.audioContextSampleRate,
    audioContextBaseLatencyMs: pcm.audioContextBaseLatencyMs,
    audioContextOutputLatencyMs: pcm.audioContextOutputLatencyMs,
    lastError: pcm.lastError === "-" ? "" : pcm.lastError,
    pcmWorkletReady: pcm.audioWorkletProcessorReady,
    pcmRingReady: pcm.ringBufferReady
  });
}

async function setupPcmAudio() {
  resetPcmReceiver();
  pcm.state = "STARTING_AUDIO";
  pcm.secureContextOk = window.isSecureContext;
  if (!pcm.secureContextOk) {
    throw new Error("Lossless PCM requires a secure HTTPS context.");
  }

  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (!AudioContextClass) {
    throw new Error("This browser does not support AudioContext.");
  }

  pcmAudioContext = new AudioContextClass({ sampleRate: 48000, latencyHint: "interactive" });
  await pcmAudioContext.resume();
  pcm.audioContextSampleRate = Number(pcmAudioContext.sampleRate || 0);
  pcm.audioContextBaseLatencyMs = Number.isFinite(Number(pcmAudioContext.baseLatency))
    ? Number(pcmAudioContext.baseLatency) * 1000
    : -1;
  pcm.audioContextOutputLatencyMs = Number.isFinite(Number(pcmAudioContext.outputLatency))
    ? Number(pcmAudioContext.outputLatency) * 1000
    : -1;
  console.info("PGStream AudioContext", {
    sampleRate: pcm.audioContextSampleRate,
    baseLatencyMs: pcm.audioContextBaseLatencyMs,
    outputLatencyMs: pcm.audioContextOutputLatencyMs
  });
  if (Math.round(pcmAudioContext.sampleRate) !== 48000) {
    throw new Error(`PCM playback requires a 48 kHz AudioContext; browser opened ${pcmAudioContext.sampleRate} Hz.`);
  }

  pcm.audioWorkletAvailable = !!pcmAudioContext.audioWorklet;
  if (!pcm.audioWorkletAvailable) {
    throw new Error("This browser does not support AudioWorklet.");
  }

  await pcmAudioContext.audioWorklet.addModule("/pcm-worklet.js");
  pcm.audioWorkletModuleLoaded = true;

  pcmWorkletNode = new AudioWorkletNode(pcmAudioContext, "pgstream-pcm-player", {
    numberOfOutputs: 1,
    outputChannelCount: [2]
  });
  pcm.audioWorkletNodeCreated = true;

  const ready = new Promise((resolve) => {
    pcmWorkletNode.port.onmessage = (event) => {
      const message = event.data || {};
      if (message.type === "processor-ready") {
        pcm.audioWorkletProcessorReady = true;
        pcm.usingSab = !!message.usingSab;
        pcm.state = "WORKLET_READY";
        resolve();
      } else if (message.type === "processor-stats") {
        pcm.underflows = Number(message.underflows || 0);
        pcm.bufferFrames = Number(message.bufferFrames || pcm.bufferFrames || 0);
        if (pcm.state === "RUNNING" && pcm.bufferFrames === 0 && pcm.underflows > 0) {
          pcm.state = "UNDERFLOW";
        }
      }
    };
  });

  const canUseSab = pcm.sabAvailable && pcm.crossOriginIsolated;
  if (!canUseSab) {
    throw new Error("PCM playback requires SharedArrayBuffer/cross-origin isolation for the browser playback ring.");
  }
  pcm.ringCapacityFrames = pcmRingCapacityFrames;
  const headerBuffer = new SharedArrayBuffer(Int32Array.BYTES_PER_ELEMENT * 4);
  const leftBuffer = new SharedArrayBuffer(Float32Array.BYTES_PER_ELEMENT * pcm.ringCapacityFrames);
  const rightBuffer = new SharedArrayBuffer(Float32Array.BYTES_PER_ELEMENT * pcm.ringCapacityFrames);
  pcm.header = new Int32Array(headerBuffer);
  pcm.ringLeft = new Float32Array(leftBuffer);
  pcm.ringRight = new Float32Array(rightBuffer);
  Atomics.store(pcm.header, 0, 0);
  Atomics.store(pcm.header, 1, 0);
  Atomics.store(pcm.header, 2, pcm.ringCapacityFrames);
  Atomics.store(pcm.header, 3, 0);
  pcmWorkletNode.port.postMessage({
    type: "configure-sab",
    header: headerBuffer,
    left: leftBuffer,
    right: rightBuffer
  });

  pcmWorkletNode.connect(pcmAudioContext.destination);
  await ready;
  pcm.ringBufferReady = true;
}

function configurePcmDataChannel(channel) {
  pcmDataChannel = channel;
  pcmDataChannel.binaryType = "arraybuffer";
  pcm.dataChannelState = pcmDataChannel.readyState;

  pcmDataChannel.onopen = () => {
    pcm.dataChannelState = "open";
    pcm.state = pcm.audioWorkletProcessorReady ? "WORKLET_READY" : "DATACHANNEL_OPEN";
    sendPcmReceiverStats();
    renderUi();
  };

  pcmDataChannel.onclose = () => {
    pcm.dataChannelState = "closed";
    pcm.state = running && !stopping ? "FAILED" : "IDLE";
    pcm.lastReason = "DataChannel closed";
    renderUi();
  };

  pcmDataChannel.onerror = () => {
    pcm.dataChannelState = "error";
    pcm.state = "FAILED";
    pcm.lastError = "DataChannel error";
    renderUi();
  };

  pcmDataChannel.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) {
      handlePcmData(event.data);
      return;
    }
    if (event.data && event.data.arrayBuffer) {
      event.data.arrayBuffer().then(handlePcmData).catch(() => {
        pcm.droppedInvalid += 1;
      });
    }
  };
}

async function startWebRtc() {
  currentEngine = resolveEngineForStart();
  if (isPcmEngine()) {
    await startPcmWebRtc();
  } else {
    await startOpusWebRtc();
  }
}

async function startOpusWebRtc() {
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
      transportMode: "WebRTC Opus",
      autoMode: els.autoModeSelect.value,
      profileSource: activeProfile.source === "auto" ? "autonegotiation" : "browser_user"
    });
  };
}

async function startPcmWebRtc() {
  rtcStats.codec = pcmFormatLabel();
  markProfileApplyPending();
  await setupPcmAudio();

  pc = new RTCPeerConnection({ iceServers: [] });
  const channel = pc.createDataChannel("pgs-pcm-audio", {
    ordered: false,
    maxRetransmits: 0
  });
  configurePcmDataChannel(channel);

  pc.onconnectionstatechange = () => {
    rtcStats.connectionState = pc.connectionState;
    if (pc.connectionState === "connected") setStatus(pcm.dataChannelState === "open" ? "connected" : "connecting");
    if (pc.connectionState === "failed") {
      setStatus("failed");
      pcm.state = "FAILED";
      pcm.lastReason = "WebRTC connection failed";
    }
    if (pc.connectionState === "disconnected") {
      setStatus("disconnected");
      pcm.lastReason = "WebRTC disconnected";
    }
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
        mid: event.candidate.sdpMid || "0"
      });
    }
  };

  const signaling = createSocket();
  signaling.onopen = async () => {
    try {
      setStatus("signaling");
      const offer = await pc.createOffer();
      if (!offer.sdp.includes("m=application") || !offer.sdp.includes("a=sctp-port")) {
        throw new Error("PCM WebRTC offer did not include SCTP DataChannel media.");
      }
      await pc.setLocalDescription(offer);
      sendJson({
        type: "webrtc-offer",
        sdpType: pc.localDescription.type,
        sdp: pc.localDescription.sdp,
        bitrateBps: activeProfile.bitrateBps,
        bitratePreset: activeProfile.bitratePreset,
        latencyPreset: activeProfile.latencyPreset,
        transportMode: transportModeForEngine(),
        autoMode: els.autoModeSelect.value,
        profileSource: activeProfile.source === "auto" ? "autonegotiation" : "browser_user"
      });
      sendPcmReceiverStats();
    } catch (error) {
      pcm.state = "FAILED";
      pcm.lastError = error.message || "PCM signaling failed";
      els.warning.textContent = pcm.lastError;
      setStatus("error");
      renderUi();
    }
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

  if (pcmDataChannel) {
    pcmDataChannel.onopen = null;
    pcmDataChannel.onclose = null;
    pcmDataChannel.onerror = null;
    pcmDataChannel.onmessage = null;
    if (pcmDataChannel.readyState === "open" || pcmDataChannel.readyState === "connecting") {
      pcmDataChannel.close();
    }
    pcmDataChannel = null;
  }

  if (pcmWorkletNode) {
    pcmWorkletNode.port.postMessage({ type: "set-running", running: false });
    pcmWorkletNode.disconnect();
    pcmWorkletNode = null;
  }

  if (pcmAudioContext) {
    const closingContext = pcmAudioContext;
    pcmAudioContext = null;
    closingContext.close().catch(() => {});
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

    if (isPcmEngine()) {
      const now = performance.now();
      if (rtcStats.lastBytesAt > 0 && now > rtcStats.lastBytesAt && pcm.bytesReceived >= rtcStats.lastBytes) {
        rtcStats.actualBitrateKbps = ((pcm.bytesReceived - rtcStats.lastBytes) * 8) / (now - rtcStats.lastBytesAt);
      }
      rtcStats.lastBytes = pcm.bytesReceived;
      rtcStats.lastBytesAt = now;
      rtcStats.codec = pcmFormatLabel();
      rtcStats.packetsReceived = pcm.packetsReceived;
      rtcStats.bytesReceived = pcm.bytesReceived;
      rtcStats.packetsLost = pcm.droppedInvalid + pcm.droppedDuplicate + pcm.droppedStale + pcm.missingPackets + pcm.latePackets;
      rtcStats.packetsReceivedIncreasing = pcm.packetsReceived > 0;

      const pcmInbound = {
        id: `pcm-${pcm.streamId || 0}`,
        packetsReceived: pcm.packetsReceived,
        packetsLost: rtcStats.packetsLost,
        packetsReceivedIncreasing: pcm.packetsReceived > 0 && pcmBufferMs() >= Math.max(5, pcm.targetBufferMs * 0.5)
      };

      updateAutoNegotiation(pcmInbound).catch((error) => {
        autoNegotiation.active = false;
        autoNegotiation.state = "error";
        autoNegotiation.lastFailure = error.message || "auto negotiation error";
        els.warning.textContent = autoNegotiation.lastFailure;
        renderUi();
      });

      sendPcmReceiverStats();
      renderUi();
      return;
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
  els.transport.textContent = server.transport || transportModeForEngine();
  els.codec.textContent = rtcStats.codec;
  els.bitrate.textContent = isPcmEngine() ? pcmFormatLabel() : activeProfile.bitratePreset;
  els.lastReason.textContent = server.lastAdaptationReason || autoNegotiation.lastFailure || "-";
  els.receiverProtocol.textContent = window.location.protocol;
  els.secureContext.textContent = window.isSecureContext ? "yes" : "no";
  els.audioWorkletAvailable.textContent = (window.AudioContext || window.webkitAudioContext) ? "available" : "missing";
  const contextSampleRate = pcm.audioContextSampleRate || server.pcmAudioContextSampleRate || 0;
  const contextBaseLatency = pcm.audioContextBaseLatencyMs >= 0 ? pcm.audioContextBaseLatencyMs : server.pcmAudioContextBaseLatencyMs;
  const contextOutputLatency = pcm.audioContextOutputLatencyMs >= 0 ? pcm.audioContextOutputLatencyMs : server.pcmAudioContextOutputLatencyMs;
  els.audioContextSampleRate.textContent = contextSampleRate > 0 ? `${contextSampleRate.toFixed(0)} Hz` : "-";
  els.audioContextBaseLatency.textContent = contextBaseLatency >= 0 ? `${contextBaseLatency.toFixed(2)} ms` : "-";
  els.audioContextOutputLatency.textContent = contextOutputLatency >= 0 ? `${contextOutputLatency.toFixed(2)} ms` : "-";
  els.signalingScheme.textContent = window.location.protocol === "https:" ? "wss" : "ws";
  els.receiverScheme.textContent = server.serverScheme || (window.location.protocol === "https:" ? "https" : "http");
  els.certificateMode.textContent = server.selfSignedCertificateEnabled ? "self-signed" : "disabled";

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

  if (els.pcmEngine) {
    pcm.queuedBytes = pcmDataChannel ? Number(pcmDataChannel.bufferedAmount || 0) : 0;
    els.pcmEngine.textContent = server.transport || transportModeForEngine();
    els.pcmDataChannelState.textContent = pcmDataChannel ? pcmDataChannel.readyState : pcm.dataChannelState;
    els.pcmQueuedBytes.textContent = String(Math.max(pcm.queuedBytes, server.pcmDataChannelBufferedBytes || 0));
    els.pcmWorkletState.textContent = `${pcm.state} ${pcm.audioWorkletProcessorReady ? "ready" : "not ready"}`;
    els.pcmSecureContext.textContent = pcm.secureContextOk ? "yes" : "no";
    els.pcmSabState.textContent = `${pcm.usingSab ? "SAB" : "queue"} / ${pcm.crossOriginIsolated ? "isolated" : "not isolated"}`;
    els.pcmBufferMs.textContent = `${pcmBufferMs().toFixed(1)} ms`;
    els.pcmTargetBufferMs.textContent = `${pcm.targetBufferMs.toFixed(0)} ms`;
    els.pcmPackets.textContent = `${pcm.packetsReceived} rx / ${server.pcmPacketsSent} tx`;
    els.pcmStreamInfo.textContent = `id ${pcm.streamId || "-"} / ${pcm.sampleRate || "-"} Hz`;
    els.pcmPacketDuration.textContent = `${pcm.packetDurationMs.toFixed(1)} ms (${pcm.packetFrames} frames)`;
    els.pcmDrops.textContent = `${pcm.droppedInvalid + pcm.droppedDuplicate + pcm.droppedStale + pcm.overflows} / ${pcm.underflows}`;
    els.pcmPacketGaps.textContent = `${pcm.missingPackets} / ${pcm.latePackets}`;
    els.pcmAckAge.textContent = server.pcmReceiverAckAgeMs >= 0 ? `${server.pcmReceiverAckAgeMs.toFixed(0)} ms` : "-";
    els.pcmLastReason.textContent = pcm.lastError !== "-" ? pcm.lastError : (server.pcmReceiverLastError || pcm.lastReason || "-");
  }

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
    ? `${autoNegotiation.profilesTested}/${autoNegotiation.candidates.length || autoMaxProfiles}`
    : `0/${autoNegotiation.candidates.length || autoMaxProfiles}`;
  els.activeEncoderBitrate.textContent = `${formatBitrateKbps(server.opusBitrateBps)}`
    + ` rev ${server.stateRevision} ${server.stateOrigin}`;
  els.activeOpusFrame.textContent = `${server.opusFrameDurationMs} ms`;
  els.autoProfileChanges.textContent = String(autoNegotiation.profileChanges);
  els.autoFifoUnderruns.textContent = String(server.fifoUnderruns);
  els.autoFinalProfile.textContent = autoNegotiation.finalProfile;
  els.autoLastFailure.textContent = autoNegotiation.lastFailure === "-"
    ? `${autoNegotiation.readiness}; native ${server.lastAdaptationReason}; rx ${lastStateUpdateReceivedTimestamp}; suppressed ${uiSyncSuppressedEventCount}`
    : autoNegotiation.lastFailure;

  if (!server.httpsEnabled && selectedEngine() !== "opus") {
    setEngineControl("opus");
  }

  const selectedIsPcm = isPcmEngine(selectedEngine());
  if (els.bitrateReadout) {
    els.bitrateReadout.hidden = !selectedIsPcm;
    els.bitrateReadout.textContent = pcmFormatLabel(selectedEngine());
  }
  els.bitrateSelect.hidden = selectedIsPcm;
  els.bitrateSelect.disabled = selectedIsPcm || autoNegotiation.active || profileApplyInProgress;
  els.latencySelect.disabled = autoNegotiation.active || profileApplyInProgress;
  els.autoModeSelect.disabled = autoNegotiation.active;
  els.autoButton.textContent = autoNegotiation.active ? "Cancel Auto" : "Auto Negotiate";

  if (els.engineSelect) {
    els.engineSelect.disabled = !server.httpsEnabled || autoNegotiation.active || profileApplyInProgress;
    for (const option of els.engineSelect.options) {
      if (option.value === "pcm16" || option.value === "pcm24" || option.value === "pcm32f") {
        option.disabled = !server.httpsEnabled;
      }
    }
  }

  if (!server.httpsEnabled) {
    els.warning.textContent = "Lossless PCM requires self-signed HTTPS. Enable it in Settings to use PCM16/PCM24/PCM32F.";
  }
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
  sendStreamControlUpdate("profile");

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
els.engineSelect.addEventListener("change", () => {
  if (suppressControlEvents > 0) {
    uiSyncSuppressedEventCount += 1;
    return;
  }

  currentEngine = selectedEngine();
  profileUserSelected = true;
  if (isPcmEngine(currentEngine) && !server.httpsEnabled) {
    els.warning.textContent = "Lossless PCM requires self-signed HTTPS. Enable it in Settings to use PCM16/PCM24/PCM32F.";
  } else {
    els.warning.textContent = defaultWarning;
  }
  sendStreamControlUpdate("engine");
  if (running && !stopping && !autoNegotiation.active) {
    reconnectForManualProfileChange().catch((error) => {
      profileApplyInProgress = false;
      els.warning.textContent = error.message || "Engine reconnect failed";
      renderUi();
    });
  }
  renderUi();
});

els.autoModeSelect.addEventListener("change", () => {
  if (suppressControlEvents > 0) {
    uiSyncSuppressedEventCount += 1;
    return;
  }

  autoNegotiation.mode = els.autoModeSelect.value;
  sendStreamControlUpdate("auto-mode");
  renderUi();
});

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
