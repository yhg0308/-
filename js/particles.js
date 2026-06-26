/**
 * Particle System — Blood splatter, smoke wisps, electric arcs, ash
 *
 * All particles rendered on Canvas for performance.
 */

const PARTICLE_TYPES = {
  BLOOD: 'blood',
  SMOKE: 'smoke',
  ASH: 'ash',
  SPARK: 'spark',
};

class Particle {
  constructor(type, x, y) {
    this.type = type;
    this.x = x;
    this.y = y;
    this.vx = 0;
    this.vy = 0;
    this.life = 0;       // seconds remaining
    this.maxLife = 0;
    this.size = 0;
    this.color = '';
    this.alpha = 1;
    this.rotation = 0;
    this.rotSpeed = 0;
    this.gravity = 0;

    this._initByType();
  }

  _initByType() {
    switch (this.type) {
      case PARTICLE_TYPES.BLOOD:
        this.size = 2 + Math.random() * 4;
        this.life = 0.3 + Math.random() * 0.5;
        this.maxLife = this.life;
        const angle = Math.random() * Math.PI * 2;
        const speed = 50 + Math.random() * 150;
        this.vx = Math.cos(angle) * speed;
        this.vy = Math.sin(angle) * speed - 30;
        this.gravity = 200;
        this.color = `hsl(${0 + Math.random() * 10}, 80%, ${30 + Math.random() * 20}%)`;
        break;

      case PARTICLE_TYPES.SMOKE:
        this.size = 8 + Math.random() * 12;
        this.life = 1.5 + Math.random() * 2.0;
        this.maxLife = this.life;
        this.vx = (Math.random() - 0.5) * 15;
        this.vy = -20 - Math.random() * 40;
        this.gravity = -5; // slight upward drift
        this.rotSpeed = (Math.random() - 0.5) * 2;
        this.color = `rgba(180, 200, 210, `;
        break;

      case PARTICLE_TYPES.ASH:
        this.size = 1.5 + Math.random() * 3;
        this.life = 0.4 + Math.random() * 0.6;
        this.maxLife = this.life;
        const ashAngle = Math.random() * Math.PI * 2;
        const ashSpeed = 20 + Math.random() * 60;
        this.vx = Math.cos(ashAngle) * ashSpeed;
        this.vy = Math.sin(ashAngle) * ashSpeed - 40;
        this.gravity = 80;
        this.color = `rgba(180, 170, 160, `;
        break;

      case PARTICLE_TYPES.SPARK:
        this.size = 1 + Math.random() * 2.5;
        this.life = 0.1 + Math.random() * 0.25;
        this.maxLife = this.life;
        const sparkAngle = Math.random() * Math.PI * 2;
        const sparkSpeed = 80 + Math.random() * 250;
        this.vx = Math.cos(sparkAngle) * sparkSpeed;
        this.vy = Math.sin(sparkAngle) * sparkSpeed;
        this.color = `rgba(150, 200, 255, `;
        break;
    }
  }

  /**
   * Update particle
   * @param {number} dt
   * @returns {boolean} — true if still alive
   */
  update(dt) {
    this.life -= dt;
    if (this.life <= 0) return false;

    this.vy += this.gravity * dt;
    this.x += this.vx * dt;
    this.y += this.vy * dt;
    this.rotation += this.rotSpeed * dt;

    // Fade out as life decreases
    this.alpha = Math.max(0, this.life / this.maxLife);

    return true;
  }

  /**
   * Render particle on canvas context
   */
  render(ctx) {
    ctx.save();
    ctx.globalAlpha = this.alpha;

    switch (this.type) {
      case PARTICLE_TYPES.BLOOD:
        ctx.fillStyle = this.color.replace('hsl', 'hsla').replace('%)', `%, ${this.alpha})`);
        if (this.color.startsWith('hsl')) {
          ctx.fillStyle = this.color;
          ctx.globalAlpha = this.alpha;
        }
        ctx.beginPath();
        ctx.arc(this.x, this.y, this.size, 0, Math.PI * 2);
        ctx.fill();
        break;

      case PARTICLE_TYPES.SMOKE:
        ctx.fillStyle = this.color + `${this.alpha * 0.5})`;
        ctx.beginPath();
        ctx.arc(this.x, this.y, this.size * (1 + (1 - this.alpha) * 1.5), 0, Math.PI * 2);
        ctx.fill();
        break;

      case PARTICLE_TYPES.ASH:
        ctx.fillStyle = this.color + `${this.alpha})`;
        ctx.fillRect(this.x - this.size / 2, this.y - this.size / 2, this.size, this.size);
        break;

      case PARTICLE_TYPES.SPARK:
        ctx.strokeStyle = this.color + `${this.alpha})`;
        ctx.lineWidth = this.size;
        ctx.beginPath();
        const sparkLen = this.size * 3;
        ctx.moveTo(this.x, this.y);
        ctx.lineTo(
          this.x + Math.cos(this.rotation) * sparkLen,
          this.y + Math.sin(this.rotation) * sparkLen
        );
        ctx.stroke();
        break;
    }

    ctx.restore();
  }
}

class ParticleSystem {
  constructor() {
    this.particles = [];
  }

  reset() {
    this.particles = [];
  }

  /**
   * Emit a burst of particles
   */
  emit(type, x, y, count = 10) {
    for (let i = 0; i < count; i++) {
      this.particles.push(new Particle(type, x, y));
    }
  }

  /**
   * Emit blood splatter
   */
  emitBlood(x, y) {
    this.emit(PARTICLE_TYPES.BLOOD, x, y, 12 + Math.floor(Math.random() * 8));
  }

  /**
   * Emit electric zap VFX (ash + sparks)
   */
  emitZap(x, y) {
    this.emit(PARTICLE_TYPES.ASH, x, y, 8);
    this.emit(PARTICLE_TYPES.SPARK, x, y, 15);
  }

  /**
   * Emit smoke from a coil
   */
  emitSmoke(x, y) {
    this.emit(PARTICLE_TYPES.SMOKE, x, y, 1);
  }

  /**
   * Update all particles
   */
  update(dt) {
    this.particles = this.particles.filter(p => p.update(dt));
  }

  /**
   * Render all particles
   */
  render(ctx) {
    for (const p of this.particles) {
      p.render(ctx);
    }
  }

  get count() {
    return this.particles.length;
  }
}

// Singleton
const Particles = new ParticleSystem();
