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
    this.playbackEnabled = false;
    this.playbackState = "buffering";
    this.targetBufferFrames = Math.max(128, Math.round(sampleRate * 0.1));
    this.resumeBufferFrames = this.targetBufferFrames;

    this.port.onmessage = (event) => {
      const message = event.data || {};
      if (message.type === "configure-sab") {
        this.header = new Int32Array(message.header);
        this.left = new Float32Array(message.left);
        this.right = new Float32Array(message.right);
        this.usingSab = true;
        this.playbackEnabled = false;
        this.running = false;
        this.playbackState = "buffering";
        this.configureBuffers(message);
        this.port.postMessage({ type: "processor-ready", usingSab: true });
        return;
      }

      if (message.type === "reset") {
        this.playbackEnabled = false;
        this.running = false;
        this.playbackState = "buffering";
        this.underflows = 0;
        if (this.header) {
          Atomics.store(this.header, 0, 0);
          Atomics.store(this.header, 1, 0);
          Atomics.store(this.header, 3, 0);
        }
        return;
      }

      if (message.type === "set-buffer-config") {
        this.configureBuffers(message);
        return;
      }

      if (message.type === "set-running") {
        this.playbackEnabled = !!message.running;
        this.running = !!message.running;
        this.playbackState = this.playbackEnabled ? "playing" : "buffering";
        if (this.header) {
          Atomics.store(this.header, 3, this.running ? 1 : 0);
        }
      }
    };
  }

  configureBuffers(message) {
    const target = Number(message.targetBufferFrames || 0);
    const resume = Number(message.resumeBufferFrames || 0);
    if (Number.isFinite(target) && target > 0) {
      this.targetBufferFrames = Math.max(128, Math.round(target));
    }
    if (Number.isFinite(resume) && resume > 0) {
      this.resumeBufferFrames = Math.max(128, Math.round(resume));
    } else {
      this.resumeBufferFrames = this.targetBufferFrames;
    }
  }

  availableFrames() {
    if (this.usingSab && this.header) {
      return Math.max(0, Atomics.load(this.header, 0) - Atomics.load(this.header, 1));
    }

    return 0;
  }

  postStats(extra = {}) {
    this.port.postMessage({
      type: "processor-stats",
      underflows: this.underflows,
      bufferFrames: this.availableFrames(),
      running: this.running,
      playbackState: this.playbackState,
      usingSab: this.usingSab,
      ...extra
    });
  }

  fillSilence(left, right, start = 0) {
    for (let i = start; i < left.length; i += 1) {
      left[i] = 0;
      right[i] = 0;
    }
  }

  readFrames(left, right, framesToRead) {
    const read = Atomics.load(this.header, 1);
    const capacity = Atomics.load(this.header, 2);
    for (let i = 0; i < framesToRead; i += 1) {
      const frame = (read + i) % capacity;
      left[i] = this.left[frame] || 0;
      right[i] = this.right[frame] || 0;
    }
    Atomics.store(this.header, 1, read + framesToRead);
  }

  process(inputs, outputs) {
    try {
      const output = outputs[0];
      if (!output || output.length === 0) return true;

      const left = output[0];
      const right = output[1] || output[0];

      if (!this.playbackEnabled || !this.usingSab || !this.header || !this.left || !this.right) {
        this.fillSilence(left, right);
      } else if (this.playbackState === "buffering" && this.availableFrames() < this.resumeBufferFrames) {
        this.running = false;
        Atomics.store(this.header, 3, 0);
        this.fillSilence(left, right);
      } else {
        if (this.playbackState === "buffering") {
          this.playbackState = "playing";
          this.running = true;
          Atomics.store(this.header, 3, 1);
          this.postStats({ resumed: true });
        }

        const requested = left.length;
        const available = this.availableFrames();
        if (available < requested) {
          if (available > 0) {
            this.readFrames(left, right, available);
          }
          this.fillSilence(left, right, available);
          this.underflows += 1;
          this.playbackState = "buffering";
          this.running = false;
          Atomics.store(this.header, 3, 0);
          this.postStats({ underrun: true });
        } else {
          this.readFrames(left, right, requested);
        }
      }

      this.framesPlayed += left.length;
      if (this.framesPlayed - this.lastStatsFrame >= sampleRate / 4) {
        this.lastStatsFrame = this.framesPlayed;
        this.postStats();
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
