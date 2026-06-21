"use strict";

const els = {
  start: document.getElementById("startButton"),
  nerd: document.getElementById("nerdButton"),
  nerdPanel: document.getElementById("nerdPanel"),
  bitrateSelect: document.getElementById("bitrateSelect"),
  latencySelect: document.getElementById("latencySelect"),
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
  rtpAttempts: document.getElementById("rtpAttempts")
};

const defaultWarning = els.warning.textContent.trim();

let socket = null;
let pc = null;
let remoteStream = null;
let remoteTrack = null;
let uiTimer = null;
let serverTimer = null;
let rtcStatsTimer = null;
let running = false;
let stopping = false;

const client = {
  status: "stopped"
};

const rtcStats = {
  connectionState: "closed",
  iceConnectionState: "closed",
  codec: "opus/48000/2",
  packetsReceived: 0,
  packetsLost: 0,
  jitterMs: 0,
  roundTripTimeMs: null,
  concealedSamples: null,
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
  latencyTarget: "about 40-90 ms",
  senderQueueFillMs: 0,
  senderDroppedFrames: 0,
  encoderOverloads: 0,
  webrtcOpenTracks: 0,
  rtpAttempts: 0,
  rtpSent: 0,
  rtpFailures: 0
};

function setStatus(status) {
  client.status = status;
}

function resetClientStats() {
  client.status = "stopped";
  rtcStats.connectionState = "closed";
  rtcStats.iceConnectionState = "closed";
  rtcStats.packetsReceived = 0;
  rtcStats.packetsLost = 0;
  rtcStats.jitterMs = 0;
  rtcStats.roundTripTimeMs = null;
  rtcStats.concealedSamples = null;
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
  if (info.latencyPreset) server.latencyPreset = info.latencyPreset;
  if (info.latencyTarget) server.latencyTarget = info.latencyTarget;

  if (!running) {
    if (info.opusBitrateBps) {
      els.bitrateSelect.value = String(info.opusBitrateBps);
    }

    if (info.latencyPreset) {
      const lower = String(info.latencyPreset).toLowerCase();
      els.latencySelect.value = lower.includes("ultra") ? "ultra"
        : lower.includes("low") ? "low"
        : lower.includes("safe") ? "safe"
        : "balanced";
    }
  }

  if (info.server) {
    server.senderQueueFillMs = Number(info.server.senderQueueFillMs || 0);
    server.senderDroppedFrames = Number(info.server.senderDroppedFrames || 0);
    server.encoderOverloads = Number(info.server.webrtcEncoderOverloadWarnings || 0);
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
    rtcStatsTimer = window.setInterval(refreshRtcStats, 500);
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

  pc = new RTCPeerConnection({ iceServers: [] });
  pc.addTransceiver("audio", { direction: "recvonly" });

  pc.ontrack = (event) => {
    remoteTrack = event.track;
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
      bitrateBps: Number(els.bitrateSelect.value),
      bitratePreset: bitrateLabel(Number(els.bitrateSelect.value)),
      latencyPreset: latencyLabel(els.latencySelect.value)
    });
  };
}

async function startAudio() {
  if (running || stopping) return;

  resetClientStats();
  running = true;
  stopping = false;
  els.start.textContent = "Stop";
  els.warning.textContent = defaultWarning;
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

async function stopAudio(finalStatus = "stopped") {
  if (stopping) return;

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

async function refreshRtcStats() {
  if (!pc) return;

  try {
    const report = await pc.getStats();
    let inbound = null;
    let codec = null;
    let selectedPair = null;

    report.forEach((stat) => {
      if (stat.type === "inbound-rtp" && stat.kind === "audio" && !stat.isRemote) {
        inbound = stat;
      }

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

    if (!inbound) return;

    const now = performance.now();
    const bytes = Number(inbound.bytesReceived || 0);
    if (rtcStats.lastBytesAt > 0 && now > rtcStats.lastBytesAt && bytes >= rtcStats.lastBytes) {
      rtcStats.actualBitrateKbps = ((bytes - rtcStats.lastBytes) * 8) / (now - rtcStats.lastBytesAt);
    }
    rtcStats.lastBytes = bytes;
    rtcStats.lastBytesAt = now;

    rtcStats.packetsReceived = Number(inbound.packetsReceived || 0);
    rtcStats.packetsLost = Number(inbound.packetsLost || 0);
    rtcStats.jitterMs = Number(inbound.jitter || 0) * 1000;
    rtcStats.concealedSamples = inbound.concealedSamples ?? inbound.concealedAudio ?? null;
    rtcStats.jitterBufferDelayMs = typeof inbound.jitterBufferDelay === "number"
      ? inbound.jitterBufferDelay * 1000
      : null;
    rtcStats.jitterBufferTargetDelayMs = typeof inbound.jitterBufferTargetDelay === "number"
      ? inbound.jitterBufferTargetDelay * 1000
      : null;
  } catch {
    // Stats availability varies by browser and connection state.
  }
}

function renderUi() {
  els.connection.textContent = client.status;
  els.transport.textContent = "WebRTC Opus";
  els.codec.textContent = rtcStats.codec;
  els.bitrate.textContent = bitrateLabel(Number(els.bitrateSelect.value));

  els.rtcConnectionState.textContent = pc ? pc.connectionState : rtcStats.connectionState;
  els.iceConnectionState.textContent = pc ? pc.iceConnectionState : rtcStats.iceConnectionState;
  els.actualBitrate.textContent = `${rtcStats.actualBitrateKbps.toFixed(1)} kb/s`;
  els.packetsReceived.textContent = String(rtcStats.packetsReceived);
  els.packetsLost.textContent = String(rtcStats.packetsLost);
  els.jitter.textContent = `${rtcStats.jitterMs.toFixed(1)} ms`;
  els.roundTripTime.textContent = rtcStats.roundTripTimeMs === null ? "-" : `${rtcStats.roundTripTimeMs.toFixed(1)} ms`;
  els.concealedSamples.textContent = rtcStats.concealedSamples === null ? "-" : String(rtcStats.concealedSamples);
  els.jitterBufferDelay.textContent = rtcStats.jitterBufferDelayMs === null ? "-" : `${rtcStats.jitterBufferDelayMs.toFixed(1)} ms`;
  els.jitterBufferTargetDelay.textContent = rtcStats.jitterBufferTargetDelayMs === null ? "-" : `${rtcStats.jitterBufferTargetDelayMs.toFixed(1)} ms`;
  els.remoteTrackState.textContent = remoteTrack ? `${remoteTrack.readyState} ${remoteTrack.muted ? "muted" : "live"}` : "none";
  els.latencyPreset.textContent = `${latencyLabel(els.latencySelect.value)} (${server.latencyTarget})`;

  els.senderQueueFill.textContent = `${server.senderQueueFillMs.toFixed(1)} ms`;
  els.senderDroppedFrames.textContent = String(server.senderDroppedFrames);
  els.encoderOverloads.textContent = String(server.encoderOverloads);
  els.webrtcOpenTracks.textContent = String(server.webrtcOpenTracks);
  els.signalingState.textContent = socketStateName();
  els.rtpSent.textContent = String(server.rtpSent);
  els.rtpFailures.textContent = String(server.rtpFailures);
  els.rtpAttempts.textContent = String(server.rtpAttempts);
}

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

refreshServerInfo();
startServerTimer();
renderUi();
