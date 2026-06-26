/**
 * Audio System — Web Audio API
 * Ambient buzz with frequency variation + spatialized stereo + event SFX
 */
class AudioSystem {
  constructor() {
    this.ctx = null;
    this.initialized = false;

    // Master gain
    this.masterGain = null;

    // Ambient buzz oscillators (layered for richness)
    this.buzzOscs = [];
    this.buzzGain = null;
    this.buzzLfoGain = null;   // LFO on gain for wavering volume
    this.buzzLfoFreq = null;   // LFO on frequency for wavering pitch

    // Stereo panners for mosquito spatialization (10 left, 10 right)
    this.mosquitoPanners = [];

    // Mosquito call audio element (the mp3)
    this.mosquitoCallBuffer = null;

    // SFX buffers (generated programmatically)
    this.sfxCache = {};
  }

  /**
   * Initialize audio context (must be called from user gesture)
   */
  async init() {
    if (this.initialized) return;
    try {
      this.ctx = new (window.AudioContext || window.webkitAudioContext)();
      this.masterGain = this.ctx.createGain();
      this.masterGain.gain.value = 0.5;
      this.masterGain.connect(this.ctx.destination);

      // Pre-generate SFX immediately (CPU-only, near-instant)
      this._generateSlapSFX();
      this._generateZapSFX();
      this._generateBeepSFX();

      // Mark initialized right away — SFX and ambient buzz work now
      this.initialized = true;

      // Load mosquito call MP3 in background (large file, don't block)
      this._loadMosquitoCall().catch(e => {
        console.warn('Failed to load mosquito call mp3:', e);
      });
    } catch (e) {
      console.warn('Audio init failed:', e);
    }
  }

  async _loadMosquitoCall() {
    try {
      const resp = await fetch('assets/蚊子叫.mp3');
      const arrayBuffer = await resp.arrayBuffer();
      this.mosquitoCallBuffer = await this.ctx.decodeAudioData(arrayBuffer);
    } catch (e) {
      console.warn('Failed to load mosquito call mp3:', e);
    }
  }

  /**
   * Start ambient buzz — continuous layered oscillators with subtle random variation
   */
  startAmbientBuzz() {
    if (!this.initialized || this.buzzOscs.length > 0) return;

    this.buzzGain = this.ctx.createGain();
    this.buzzGain.gain.value = 0.06;
    this.buzzGain.connect(this.masterGain);

    // LFO modulating gain for wavering volume (0.5–2 Hz slow wobble)
    this.buzzLfoGain = this.ctx.createOscillator();
    this.buzzLfoGain.type = 'sine';
    this.buzzLfoGain.frequency.value = 0.7;
    const lfoGainNode = this.ctx.createGain();
    lfoGainNode.gain.value = 0.02;
    this.buzzLfoGain.connect(lfoGainNode);
    lfoGainNode.connect(this.buzzGain.gain);
    this.buzzLfoGain.start();

    // LFO modulating frequency for wavering pitch (1–3 Hz subtle)
    this.buzzLfoFreq = this.ctx.createOscillator();
    this.buzzLfoFreq.type = 'sine';
    this.buzzLfoFreq.frequency.value = 1.3;

    // Layer multiple oscillators at different frequencies for a rich buzz
    const baseFreqs = [180, 220, 260, 310, 370];
    for (const freq of baseFreqs) {
      const osc = this.ctx.createOscillator();
      osc.type = 'sawtooth';
      osc.frequency.value = freq;

      // Connect frequency LFO to each osc for slight pitch variation
      const freqLfoGain = this.ctx.createGain();
      freqLfoGain.gain.value = 3 + Math.random() * 5;
      this.buzzLfoFreq.connect(freqLfoGain);
      freqLfoGain.connect(osc.frequency);

      // Individual gain per harmonic (higher freqs quieter)
      const oscGain = this.ctx.createGain();
      oscGain.gain.value = (1 - (freq - 180) / 300) * 0.15;

      osc.connect(oscGain);
      oscGain.connect(this.buzzGain);

      osc.start();
      this.buzzOscs.push({ osc, gain: oscGain });
    }
    this.buzzLfoFreq.start();
  }

  /**
   * Stop ambient buzz
   */
  stopAmbientBuzz() {
    for (const { osc } of this.buzzOscs) {
      try { osc.stop(); } catch (e) { /* already stopped */ }
    }
    this.buzzOscs = [];
    if (this.buzzLfoGain) { try { this.buzzLfoGain.stop(); } catch (e) {} this.buzzLfoGain = null; }
    if (this.buzzLfoFreq) { try { this.buzzLfoFreq.stop(); } catch (e) {} this.buzzLfoFreq = null; }
    this.buzzGain = null;
  }

  /**
   * Apply low-pass filter to buzz (for pause screen blur effect)
   */
  setAmbientFiltered(filtered) {
    // TBD in main — we'll just reduce gain for now
    if (this.buzzGain) {
      this.buzzGain.gain.linearRampToValueAtTarget(
        filtered ? 0.015 : 0.06,
        this.ctx.currentTime + 0.3,
        0.1
      );
    }
    if (this.masterGain) {
      this.masterGain.gain.linearRampToValueAtTarget(
        filtered ? 0.3 : 0.5,
        this.ctx.currentTime + 0.3,
        0.1
      );
    }
  }

  /**
   * Set up spatial panners for N mosquitoes (10 left, 10 right)
   */
  setupMosquitoPanners(count) {
    if (!this.ctx) return; // Audio not initialized yet
    // Clean up old
    this.mosquitoPanners = [];
    for (let i = 0; i < count; i++) {
      const panner = this.ctx.createStereoPanner();
      // Assign: first 10 to left (-0.8 to -0.1), last 10 to right (0.1 to 0.8)
      const half = Math.floor(count / 2);
      if (i < half) {
        panner.pan.value = -0.8 + (i / (half - 1)) * 0.7; // -0.8 to -0.1
      } else {
        const j = i - half;
        const rightHalf = count - half;
        panner.pan.value = 0.1 + (j / Math.max(rightHalf - 1, 1)) * 0.7; // 0.1 to 0.8
      }
      panner.connect(this.masterGain);
      this.mosquitoPanners.push(panner);
    }
  }

  /**
   * Play a brief mosquito call from a specific mosquito (spatialized)
   */
  playMosquitoCall(index) {
    if (!this.initialized || !this.mosquitoCallBuffer || index >= this.mosquitoPanners.length) return;
    try {
      const source = this.ctx.createBufferSource();
      source.buffer = this.mosquitoCallBuffer;
      source.playbackRate.value = 0.8 + Math.random() * 0.6; // vary pitch
      const gain = this.ctx.createGain();
      gain.gain.value = 0.08 * (0.5 + Math.random() * 0.5);
      source.connect(gain);
      gain.connect(this.mosquitoPanners[index]);
      source.start(this.ctx.currentTime);
      // Auto-stop after short duration
      source.stop(this.ctx.currentTime + 0.3);
    } catch (e) { /* ignore */ }
  }

  /**
   * Generate slap SFX (hand/flyswatter hit) — short crisp "pop"
   */
  _generateSlapSFX() {
    // Use a noise burst through a bandpass filter for a crisp slap
    const duration = 0.15;
    const sampleRate = this.ctx.sampleRate;
    const length = Math.floor(sampleRate * duration);
    const buffer = this.ctx.createBuffer(1, length, sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < length; i++) {
      const t = i / sampleRate;
      const env = Math.exp(-t * 40); // fast decay
      data[i] = (Math.random() * 2 - 1) * env * 0.6;
    }
    this.sfxCache.slap = buffer;
  }

  /**
   * Generate electric zap SFX — intense "zzt" crackle
   */
  _generateZapSFX() {
    const duration = 0.35;
    const sampleRate = this.ctx.sampleRate;
    const length = Math.floor(sampleRate * duration);
    const buffer = this.ctx.createBuffer(1, length, sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < length; i++) {
      const t = i / sampleRate;
      const env = Math.exp(-t * 12);
      // Mix noise + high frequency tone for electric crackle
      const noise = (Math.random() * 2 - 1);
      const tone = Math.sin(2 * Math.PI * 800 * t) * Math.sin(2 * Math.PI * 120 * t);
      data[i] = (noise * 0.4 + tone * 0.6) * env;
    }
    this.sfxCache.zap = buffer;
  }

  /**
   * Generate low battery beep — short descending tone
   */
  _generateBeepSFX() {
    const duration = 0.25;
    const sampleRate = this.ctx.sampleRate;
    const length = Math.floor(sampleRate * duration);
    const buffer = this.ctx.createBuffer(1, length, sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < length; i++) {
      const t = i / sampleRate;
      const env = Math.exp(-t * 15);
      const freq = 300 - t * 400; // descending
      data[i] = Math.sin(2 * Math.PI * freq * t) * env * 0.5;
    }
    this.sfxCache.beep = buffer;
  }

  /**
   * Play a cached SFX
   */
  _playBuffer(buffer, volume = 0.5) {
    if (!this.initialized || !buffer) return;
    try {
      const source = this.ctx.createBufferSource();
      source.buffer = buffer;
      const gain = this.ctx.createGain();
      gain.gain.value = volume;
      source.connect(gain);
      gain.connect(this.masterGain);
      source.start(this.ctx.currentTime);
    } catch (e) { /* ignore */ }
  }

  playSlap() { this._playBuffer(this.sfxCache.slap, 0.6); }
  playZap() { this._playBuffer(this.sfxCache.zap, 0.7); }
  playBeep() { this._playBuffer(this.sfxCache.beep, 0.4); }

  /**
   * Resume context (for browser autoplay policy)
   */
  resume() {
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume();
    }
  }
}

// Singleton
const Audio = new AudioSystem();
