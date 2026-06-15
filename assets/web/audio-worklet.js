"use strict";

class PGStreamPlayer extends AudioWorkletProcessor {
  constructor() {
    super();
    this.streamSampleRate = sampleRate;
    this.contextSampleRate = sampleRate;
    this.bufferTargetMs = 500;
    this.targetBufferFrames = Math.floor(sampleRate * 0.5);
    this.criticalBufferFrames = Math.floor(sampleRate * 0.125);
    this.resumeBufferFrames = Math.floor(sampleRate * 0.375);
    this.highWaterBufferFrames = Math.floor(sampleRate * 0.75);
    this.capacityFrames = Math.max(4096, Math.floor(sampleRate * 2));
    this.buffer = new Float32Array(this.capacityFrames * 2);
    this.readFrame = 0;
    this.writeFrame = 0;
    this.framesQueued = 0;
    this.playbackState = "stopped";
    this.clientWorkletUnderruns = 0;
    this.resyncCount = 0;
    this.statusCountdown = 0;

    this.port.onmessage = (event) => this.handleMessage(event.data);
  }

  resetQueue() {
    this.readFrame = 0;
    this.writeFrame = 0;
    this.framesQueued = 0;
  }

  configure(message) {
    this.streamSampleRate = Number(message.sampleRate || sampleRate);
    this.contextSampleRate = Number(message.contextSampleRate || sampleRate);
    this.bufferTargetMs = Math.max(20, Number(message.bufferTargetMs || 100));

    this.targetBufferFrames = Math.max(1, Math.floor(this.streamSampleRate * (this.bufferTargetMs / 1000)));
    this.criticalBufferFrames = Math.max(
      Math.floor(this.streamSampleRate * 0.01),
      Math.floor(this.targetBufferFrames / 3)
    );
    this.resumeBufferFrames = Math.max(
      this.criticalBufferFrames + 1,
      this.targetBufferFrames
    );
    this.highWaterBufferFrames = this.targetBufferFrames + Math.max(
      this.targetBufferFrames,
      Math.floor(this.streamSampleRate * 0.04)
    );

    const desiredFrames = Math.max(
      4096,
      this.highWaterBufferFrames * 2
    );

    if (desiredFrames !== this.capacityFrames) {
      this.capacityFrames = desiredFrames;
      this.buffer = new Float32Array(this.capacityFrames * 2);
    }

    this.resetQueue();
    this.clientWorkletUnderruns = 0;
    this.resyncCount = 0;
    this.playbackState = "buffering";
    this.postStatus();
  }

  reset() {
    this.resetQueue();
    this.playbackState = "stopped";
    this.postStatus();
  }

  handleMessage(message) {
    if (!message || message.type === "reset") {
      this.reset();
      return;
    }

    if (message.type === "configure") {
      this.configure(message);
      return;
    }

    if (message.type !== "audio" || this.playbackState === "stopped") {
      return;
    }

    const samplesBuffer = message.samples;
    if (!samplesBuffer || message.frameCount <= 0) {
      return;
    }

    const interleaved = new Float32Array(samplesBuffer);
    const incomingFrames = Math.min(message.frameCount, Math.floor(interleaved.length / 2));
    const queueLimit = Math.min(
      this.capacityFrames,
      Math.max(this.highWaterBufferFrames, this.targetBufferFrames + incomingFrames, incomingFrames * 2)
    );
    const overflow = Math.max(0, this.framesQueued + incomingFrames - queueLimit);
    if (overflow > 0) {
      this.readFrame = (this.readFrame + overflow) % this.capacityFrames;
      this.framesQueued -= overflow;
    }

    for (let frame = 0; frame < incomingFrames; ++frame) {
      const write = this.writeFrame * 2;
      const read = frame * 2;
      this.buffer[write] = interleaved[read];
      this.buffer[write + 1] = interleaved[read + 1];
      this.writeFrame = (this.writeFrame + 1) % this.capacityFrames;
    }

    this.framesQueued += incomingFrames;
  }

  fillSilence(left, right, start) {
    for (let i = start; i < left.length; ++i) {
      left[i] = 0;
      right[i] = 0;
    }
  }

  enterResync() {
    if (this.playbackState !== "resyncing") {
      this.playbackState = "resyncing";
      this.clientWorkletUnderruns += 1;
      this.resyncCount += 1;
      this.postStatus();
    }
  }

  updatePlaybackState() {
    if (this.playbackState === "buffering" && this.framesQueued >= this.targetBufferFrames) {
      this.playbackState = "playing";
      this.postStatus();
      return;
    }

    if (this.playbackState === "resyncing" && this.framesQueued >= this.resumeBufferFrames) {
      this.playbackState = "playing";
      this.postStatus();
    }
  }

  postStatus() {
    const rate = this.streamSampleRate || sampleRate;
    this.port.postMessage({
      type: "status",
      playbackState: this.playbackState,
      bufferMs: (this.framesQueued / rate) * 1000,
      clientBufferFillMs: (this.framesQueued / rate) * 1000,
      clientWorkletUnderruns: this.clientWorkletUnderruns,
      resyncCount: this.resyncCount
    });
  }

  process(inputs, outputs) {
    const output = outputs[0];
    const left = output[0];
    const right = output[1] || output[0];

    this.updatePlaybackState();

    if (this.playbackState !== "playing") {
      this.fillSilence(left, right, 0);
    } else {
      let i = 0;
      for (; i < left.length; ++i) {
        if (this.framesQueued <= 0) {
          this.enterResync();
          break;
        }

        const read = this.readFrame * 2;
        left[i] = this.buffer[read];
        right[i] = this.buffer[read + 1];
        this.readFrame = (this.readFrame + 1) % this.capacityFrames;
        this.framesQueued -= 1;
      }

      if (i < left.length) {
        this.fillSilence(left, right, i);
      } else if (this.framesQueued <= this.criticalBufferFrames) {
        this.enterResync();
      }
    }

    this.statusCountdown -= left.length;
    if (this.statusCountdown <= 0) {
      this.statusCountdown = Math.floor(sampleRate / 4);
      this.postStatus();
    }

    return true;
  }
}

registerProcessor("pgstream-player", PGStreamPlayer);
