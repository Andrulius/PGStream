"use strict";

class PGStreamPcmProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.running = false;
    this.usingSab = false;
    this.header = null;
    this.left = null;
    this.right = null;
    this.underflows = 0;
    this.framesPlayed = 0;
    this.lastStatsFrame = 0;

    this.port.onmessage = (event) => {
      const message = event.data || {};
      if (message.type === "configure-sab") {
        this.header = new Int32Array(message.header);
        this.left = new Float32Array(message.left);
        this.right = new Float32Array(message.right);
        this.usingSab = true;
        this.running = false;
        this.port.postMessage({ type: "processor-ready", usingSab: true });
        return;
      }

      if (message.type === "reset") {
        this.running = false;
        this.underflows = 0;
        if (this.header) {
          Atomics.store(this.header, 0, 0);
          Atomics.store(this.header, 1, 0);
          Atomics.store(this.header, 3, 0);
        }
        return;
      }

      if (message.type === "set-running") {
        this.running = !!message.running;
        if (this.header) {
          Atomics.store(this.header, 3, this.running ? 1 : 0);
        }
      }
    };
  }

  availableFrames() {
    if (this.usingSab && this.header) {
      return Math.max(0, Atomics.load(this.header, 0) - Atomics.load(this.header, 1));
    }

    return 0;
  }

  readFrame() {
    if (!this.running) return [0, 0, false];

    if (this.usingSab && this.header && this.left && this.right) {
      const write = Atomics.load(this.header, 0);
      const read = Atomics.load(this.header, 1);
      if (write <= read) return [0, 0, true];

      const capacity = Atomics.load(this.header, 2);
      const frame = read % capacity;
      const left = this.left[frame] || 0;
      const right = this.right[frame] || 0;
      Atomics.store(this.header, 1, read + 1);
      return [left, right, false];
    }

    return [0, 0, true];
  }

  process(inputs, outputs) {
    try {
      const output = outputs[0];
      if (!output || output.length === 0) return true;

      const left = output[0];
      const right = output[1] || output[0];
      for (let i = 0; i < left.length; i += 1) {
        const frame = this.readFrame();
        left[i] = frame[0];
        right[i] = frame[1];
        if (frame[2]) this.underflows += 1;
      }

      this.framesPlayed += left.length;
      if (this.framesPlayed - this.lastStatsFrame >= sampleRate / 4) {
        this.lastStatsFrame = this.framesPlayed;
        this.port.postMessage({
          type: "processor-stats",
          underflows: this.underflows,
          bufferFrames: this.availableFrames(),
          running: this.running,
          usingSab: this.usingSab
        });
      }
    } catch {
      const output = outputs[0] || [];
      for (const channel of output) {
        if (channel) channel.fill(0);
      }
    }

    return true;
  }
}

registerProcessor("pgstream-pcm-player", PGStreamPcmProcessor);
