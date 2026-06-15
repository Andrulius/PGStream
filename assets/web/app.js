"use strict";

const els = {
  start: document.getElementById("startButton"),
  nerd: document.getElementById("nerdButton"),
  nerdPanel: document.getElementById("nerdPanel"),
  connection: document.getElementById("connection"),
  format: document.getElementById("format"),
  sampleRate: document.getElementById("sampleRate"),
  bufferFill: document.getElementById("bufferFill"),
  warning: document.getElementById("warning"),
  nerdStreamRate: document.getElementById("nerdStreamRate"),
  audioContextRate: document.getElementById("audioContextRate"),
  nerdFormat: document.getElementById("nerdFormat"),
  clientBufferFillMs: document.getElementById("clientBufferFillMs"),
  packetsPerSecond: document.getElementById("packetsPerSecond"),
  clientSequenceGaps: document.getElementById("clientSequenceGaps"),
  clientWorkletUnderruns: document.getElementById("clientWorkletUnderruns"),
  webSocketState: document.getElementById("webSocketState"),
  packetsTotal: document.getElementById("packetsTotal"),
  frames: document.getElementById("frames"),
  resyncCount: document.getElementById("resyncCount"),
  lastSequence: document.getElementById("lastSequence"),
  serverFifoUnderruns: document.getElementById("serverFifoUnderruns"),
  networkPacketsSent: document.getElementById("networkPacketsSent"),
  websocketSendFailures: document.getElementById("websocketSendFailures"),
  connectedClients: document.getElementById("connectedClients")
};

const defaultWarning = els.warning.textContent.trim();

let audioContext = null;
let workletNode = null;
let socket = null;
let streamInfo = null;
let uiTimer = null;
let serverTimer = null;
let running = false;
let stopping = false;

const client = {
  status: "stopped",
  audioContextRate: 0,
  clientBufferFillMs: 0,
  packetsPerSecond: 0,
  packetWindowCount: 0,
  packetWindowStartedAt: performance.now(),
  packetsTotal: 0,
  decodedFramesTotal: 0,
  clientSequenceGaps: 0,
  clientWorkletUnderruns: 0,
  resyncCount: 0,
  lastSequence: null
};

const server = {
  serverFifoUnderruns: 0,
  networkPacketsSent: 0,
  websocketSendFailures: 0,
  connectedClients: 0
};

function setStatus(status) {
  client.status = status;
}

function resetClientStats() {
  client.status = "stopped";
  client.audioContextRate = 0;
  client.clientBufferFillMs = 0;
  client.packetsPerSecond = 0;
  client.packetWindowCount = 0;
  client.packetWindowStartedAt = performance.now();
  client.packetsTotal = 0;
  client.decodedFramesTotal = 0;
  client.clientSequenceGaps = 0;
  client.clientWorkletUnderruns = 0;
  client.resyncCount = 0;
  client.lastSequence = null;
}

function fetchInfo() {
  return fetch("/info", { cache: "no-store" }).then((response) => {
    if (!response.ok) {
      throw new Error(`info request failed: ${response.status}`);
    }

    return response.json();
  });
}

function frameFormatName(format) {
  if (format === 1) return "Float32";
  if (format === 2) return "PCM16";
  return `Unknown ${format}`;
}

function socketStateName() {
  if (!socket) return "closed";
  if (socket.readyState === WebSocket.CONNECTING) return "connecting";
  if (socket.readyState === WebSocket.OPEN) return "open";
  if (socket.readyState === WebSocket.CLOSING) return "closing";
  return "closed";
}

function parseFrame(arrayBuffer) {
  if (arrayBuffer.byteLength < 28) {
    return null;
  }

  const view = new DataView(arrayBuffer);
  if (view.getUint8(0) !== 0x50 || view.getUint8(1) !== 0x47 ||
      view.getUint8(2) !== 0x53 || view.getUint8(3) !== 0x31) {
    return null;
  }

  const protocolVersion = view.getUint32(4, true);
  if (protocolVersion !== 1) {
    return null;
  }

  const frame = {
    sequence: view.getUint32(8, true),
    sampleRate: view.getUint32(12, true),
    channels: view.getUint16(16, true),
    format: view.getUint16(18, true),
    frameCount: view.getUint32(20, true),
    flags: view.getUint32(24, true),
    payload: arrayBuffer.slice(28)
  };

  return frame.channels === 2 ? frame : null;
}

function decodeFrame(frame) {
  const expectedSamples = frame.frameCount * 2;

  if (frame.format === 1) {
    if (frame.payload.byteLength % Float32Array.BYTES_PER_ELEMENT !== 0) {
      return null;
    }

    let samples = new Float32Array(frame.payload);
    if (samples.length < expectedSamples) {
      return null;
    }

    if (samples.length > expectedSamples) {
      samples = samples.slice(0, expectedSamples);
    }

    return { samples, frameCount: expectedSamples / 2 };
  }

  if (frame.format === 2) {
    if (frame.payload.byteLength % Int16Array.BYTES_PER_ELEMENT !== 0) {
      return null;
    }

    const pcm = new Int16Array(frame.payload);
    if (pcm.length < expectedSamples) {
      return null;
    }

    const samples = new Float32Array(expectedSamples);
    for (let i = 0; i < expectedSamples; ++i) {
      samples[i] = Math.max(-1, pcm[i] / 32768);
    }

    return { samples, frameCount: expectedSamples / 2 };
  }

  return null;
}

function notePacket(sequence) {
  if (client.lastSequence !== null) {
    const expected = (client.lastSequence + 1) >>> 0;
    if (sequence !== expected) {
      client.clientSequenceGaps += 1;
    }
  }

  client.lastSequence = sequence >>> 0;
  client.packetsTotal += 1;
  client.packetWindowCount += 1;
}

function updatePacketsPerSecond() {
  const now = performance.now();
  const elapsed = now - client.packetWindowStartedAt;
  if (elapsed < 1000) {
    return;
  }

  client.packetsPerSecond = client.packetWindowCount * 1000 / elapsed;
  client.packetWindowCount = 0;
  client.packetWindowStartedAt = now;
}

function applyInfo(info) {
  streamInfo = info;

  if (info.format) {
    els.format.textContent = info.format;
    els.nerdFormat.textContent = info.format;
  }

  if (info.sampleRate) {
    const text = `${info.sampleRate} Hz`;
    els.sampleRate.textContent = text;
    els.nerdStreamRate.textContent = text;
  }

  if (info.server) {
    server.serverFifoUnderruns = Number(info.server.serverFifoUnderruns || 0);
    server.networkPacketsSent = Number(info.server.networkPacketsSent || 0);
    server.websocketSendFailures = Number(info.server.websocketSendFailures || 0);
    server.connectedClients = Number(info.server.connectedClients || 0);
  }
}

async function refreshServerInfo() {
  try {
    applyInfo(await fetchInfo());
    renderUi();
  } catch {
    // The socket status is more useful than transient polling failures here.
  }
}

function handleWorkletMessage(event) {
  const message = event.data;
  if (!message || message.type !== "status") {
    return;
  }

  client.clientBufferFillMs = Number(message.clientBufferFillMs || message.bufferMs || 0);
  client.clientWorkletUnderruns = Number(message.clientWorkletUnderruns || 0);
  client.resyncCount = Number(message.resyncCount || 0);

  if (running && !stopping && message.playbackState) {
    if (socket && socket.readyState === WebSocket.OPEN) {
      setStatus(message.playbackState);
    } else if (client.status !== "error") {
      setStatus("connecting");
    }
  }
}

function handleSocketMessage(event) {
  if (!workletNode) {
    return;
  }

  const frame = parseFrame(event.data);
  if (!frame) {
    return;
  }

  const decoded = decodeFrame(frame);
  if (!decoded) {
    return;
  }

  notePacket(frame.sequence);
  client.decodedFramesTotal += decoded.frameCount;

  els.format.textContent = frameFormatName(frame.format);
  els.nerdFormat.textContent = frameFormatName(frame.format);
  els.sampleRate.textContent = `${frame.sampleRate} Hz`;
  els.nerdStreamRate.textContent = `${frame.sampleRate} Hz`;

  workletNode.port.postMessage({
    type: "audio",
    frameCount: decoded.frameCount,
    samples: decoded.samples.buffer
  }, [decoded.samples.buffer]);
}

function openSocket() {
  const wsUrl = `wss://${window.location.host}/ws`;
  socket = new WebSocket(wsUrl);
  socket.binaryType = "arraybuffer";
  socket.onopen = () => {
    if (running && !stopping) {
      setStatus("buffering");
      refreshServerInfo();
    }
  };
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
}

function startTimers() {
  if (!uiTimer) {
    uiTimer = window.setInterval(renderUi, 250);
  }

  startServerTimer();
}

function startServerTimer() {
  if (!serverTimer) {
    serverTimer = window.setInterval(refreshServerInfo, 2000);
  }
}

function stopTimers() {
  if (uiTimer) {
    window.clearInterval(uiTimer);
    uiTimer = null;
  }
}

async function startAudio() {
  if (running || stopping) {
    return;
  }

  resetClientStats();
  running = true;
  stopping = false;
  els.start.disabled = false;
  els.start.textContent = "Stop Audio";
  els.warning.textContent = defaultWarning;
  setStatus("connecting");
  startTimers();
  renderUi();

  try {
    const info = await fetchInfo();
    applyInfo(info);

    const streamRate = Number(info.sampleRate || 48000);
    const targetBufferMs = Number(info.bufferTargetMs || 100);

    audioContext = new AudioContext({ sampleRate: streamRate });
    client.audioContextRate = audioContext.sampleRate;

    await audioContext.audioWorklet.addModule("/audio-worklet.js");

    workletNode = new AudioWorkletNode(audioContext, "pgstream-player", {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2]
    });

    workletNode.port.onmessage = handleWorkletMessage;
    workletNode.port.postMessage({
      type: "configure",
      sampleRate: streamRate,
      contextSampleRate: audioContext.sampleRate,
      bufferTargetMs: targetBufferMs
    });

    workletNode.connect(audioContext.destination);
    await audioContext.resume();

    if (audioContext.sampleRate !== streamRate) {
      els.warning.textContent = `Browser chose ${audioContext.sampleRate} Hz while the stream is ${streamRate} Hz. Playback may drift on this device.`;
    }

    openSocket();
    setStatus("connecting");
  } catch (error) {
    els.warning.textContent = error.message;
    await stopAudio("error");
  }

  renderUi();
}

async function stopAudio(finalStatus = "stopped") {
  if (stopping) {
    return;
  }

  stopping = true;
  running = false;
  els.start.disabled = true;
  setStatus("stopping");
  renderUi();

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

  if (workletNode) {
    try {
      workletNode.port.postMessage({ type: "reset" });
    } catch {
      // The node may already be gone if the AudioContext closed first.
    }

    workletNode.disconnect();
    workletNode.port.onmessage = null;
    workletNode = null;
  }

  if (audioContext && audioContext.state !== "closed") {
    await audioContext.close();
  }

  audioContext = null;
  streamInfo = null;
  stopTimers();
  resetClientStats();
  setStatus(finalStatus);
  els.format.textContent = "-";
  els.sampleRate.textContent = "-";
  els.nerdFormat.textContent = "-";
  els.nerdStreamRate.textContent = "-";
  els.start.textContent = "Start Audio";
  els.start.disabled = false;
  stopping = false;
  renderUi();
}

function renderUi() {
  updatePacketsPerSecond();

  els.connection.textContent = client.status;
  els.bufferFill.textContent = `${Math.round(client.clientBufferFillMs)} ms`;
  els.clientBufferFillMs.textContent = `${Math.round(client.clientBufferFillMs)} ms`;
  els.audioContextRate.textContent = client.audioContextRate ? `${client.audioContextRate} Hz` : "-";
  els.packetsPerSecond.textContent = client.packetsPerSecond.toFixed(1);
  els.clientSequenceGaps.textContent = String(client.clientSequenceGaps);
  els.clientWorkletUnderruns.textContent = String(client.clientWorkletUnderruns);
  els.webSocketState.textContent = socketStateName();
  els.packetsTotal.textContent = String(client.packetsTotal);
  els.frames.textContent = String(client.decodedFramesTotal);
  els.resyncCount.textContent = String(client.resyncCount);
  els.lastSequence.textContent = client.lastSequence === null ? "-" : String(client.lastSequence);

  els.serverFifoUnderruns.textContent = String(server.serverFifoUnderruns);
  els.networkPacketsSent.textContent = String(server.networkPacketsSent);
  els.websocketSendFailures.textContent = String(server.websocketSendFailures);
  els.connectedClients.textContent = String(server.connectedClients);
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
