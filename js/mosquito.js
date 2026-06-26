/**
 * Mosquito Class — Individual mosquito with 3 flight modes
 *
 * Flight modes:
 *   0: HOVER — circle around a center point
 *   1: DASH  — fast straight-line burst
 *   2: BROWNIAN — random direction changes
 *
 * Each mosquito has: position, velocity, flight mode, agitation level,
 * mode timer, hover center/orbit params
 */

const FLIGHT_MODES = {
  HOVER: 0,
  DASH: 1,
  BROWNIAN: 2
};

// Game area reference (set by main.js during resize)
let GAME_W = 1920;
let GAME_H = 1080;

class Mosquito {
  constructor(index) {
    this.index = index;
    this.alive = true;

    // Position
    this.x = Math.random() * GAME_W;
    this.y = Math.random() * GAME_H;

    // Velocity
    this.vx = 0;
    this.vy = 0;

    // Base speed (px per second)
    this.baseSpeed = 80 + Math.random() * 80;

    // Current speed multiplier
    this.speedMultiplier = 1.0;

    // Flight mode
    this.mode = this._randomMode();
    this.modeTimer = 0;          // time remaining in current mode (seconds)
    this.modeDuration = 0;       // total duration for this mode

    // Hover mode state
    this.hoverCenterX = this.x;
    this.hoverCenterY = this.y;
    this.hoverRadius = 30 + Math.random() * 40;
    this.hoverAngle = Math.random() * Math.PI * 2;
    this.hoverAngularSpeed = (1.5 + Math.random() * 3) * (Math.random() < 0.5 ? 1 : -1);

    // Brownian state
    this.brownianDir = Math.random() * Math.PI * 2;
    this.brownianChangeTimer = 0;

    // Dash state
    this.dashTargetX = 0;
    this.dashTargetY = 0;

    // Rotation (visual, radians)
    this.rotation = Math.random() * Math.PI * 2;

    // Agitation (0 = calm, 1 = very agitated from player proximity)
    this.agitation = 0;

    // Visual: wing flutter offset
    this.wingPhase = Math.random() * Math.PI * 2;

    // Call timer (for audio)
    this.callCooldown = 2 + Math.random() * 5;

    // Size
    this.radius = 14; // collision radius

    // Initialize first mode
    this._enterMode(this.mode);
  }

  _randomMode() {
    const r = Math.random();
    if (r < 0.4) return FLIGHT_MODES.HOVER;
    if (r < 0.75) return FLIGHT_MODES.BROWNIAN;
    return FLIGHT_MODES.DASH;
  }

  _enterMode(mode) {
    this.mode = mode;
    switch (mode) {
      case FLIGHT_MODES.HOVER:
        // Set hover center to current position (or drift it slightly)
        this.hoverCenterX = this.x + (Math.random() - 0.5) * 60;
        this.hoverCenterY = this.y + (Math.random() - 0.5) * 60;
        this.hoverRadius = 25 + Math.random() * 50;
        this.hoverAngularSpeed = (1.5 + Math.random() * 3) * (Math.random() < 0.5 ? 1 : -1);
        this.modeDuration = 1.5 + Math.random() * 2.5;
        break;

      case FLIGHT_MODES.DASH:
        // Pick a direction and go fast
        const angle = Math.random() * Math.PI * 2;
        const dashDist = 100 + Math.random() * 250;
        this.dashTargetX = this.x + Math.cos(angle) * dashDist;
        this.dashTargetY = this.y + Math.sin(angle) * dashDist;
        this.vx = Math.cos(angle) * this.baseSpeed * 2.5;
        this.vy = Math.sin(angle) * this.baseSpeed * 2.5;
        this.modeDuration = 0.4 + Math.random() * 0.8;
        break;

      case FLIGHT_MODES.BROWNIAN:
        this.brownianDir = Math.atan2(this.vy || 0.001, this.vx);
        this.brownianChangeTimer = 0;
        this.modeDuration = 1.0 + Math.random() * 2.0;
        break;
    }
    this.modeTimer = this.modeDuration;
  }

  /**
   * Update mosquito state for deltaTime seconds
   * @param {number} dt - Delta time in seconds
   * @param {number} playerX - Player cursor X (for agitation calc)
   * @param {number} playerY - Player cursor Y
   * @param {number} globalSpeedBonus - Speed boost from low stamina (0-1)
   */
  update(dt, playerX, playerY, globalSpeedBonus) {
    if (!this.alive) return;

    // Update mode timer
    this.modeTimer -= dt;
    if (this.modeTimer <= 0) {
      this._enterMode(this._randomMode());
    }

    // Calculate agitation based on player proximity
    const dx = this.x - playerX;
    const dy = this.y - playerY;
    const distToPlayer = Math.sqrt(dx * dx + dy * dy);
    const agitationTarget = distToPlayer < 200 ? (1 - distToPlayer / 200) : 0;
    this.agitation += (agitationTarget - this.agitation) * Math.min(dt * 4, 1);

    // Speed multiplier: base + agitation boost + global stamina bonus
    this.speedMultiplier = 1.0 + this.agitation * 0.3 + globalSpeedBonus;

    // Move based on mode
    switch (this.mode) {
      case FLIGHT_MODES.HOVER:
        this._updateHover(dt);
        break;
      case FLIGHT_MODES.DASH:
        this._updateDash(dt);
        break;
      case FLIGHT_MODES.BROWNIAN:
        this._updateBrownian(dt);
        break;
    }

    // Boundary bouncing with angle reflection
    this._handleBoundaries();

    // Smooth rotation toward movement direction
    const targetRot = Math.atan2(this.vy, this.vx);
    const rotDiff = targetRot - this.rotation;
    // Normalize
    const normalizedDiff = ((rotDiff + Math.PI) % (Math.PI * 2)) - Math.PI;
    this.rotation += normalizedDiff * Math.min(dt * 8, 1);

    // Wing phase
    this.wingPhase += dt * 25;

    // Audio call timer
    this.callCooldown -= dt;
    if (this.callCooldown <= 0) {
      this.callCooldown = 2 + Math.random() * 8;
      // Audio call will be triggered by main.js checking this
    }
  }

  _updateHover(dt) {
    // Orbit around hover center
    this.hoverAngle += this.hoverAngularSpeed * this.speedMultiplier * dt;
    const targetX = this.hoverCenterX + Math.cos(this.hoverAngle) * this.hoverRadius;
    const targetY = this.hoverCenterY + Math.sin(this.hoverAngle) * this.hoverRadius;

    // Smooth movement toward orbit position
    const lerpFactor = 5 * dt;
    this.x += (targetX - this.x) * lerpFactor;
    this.y += (targetY - this.y) * lerpFactor;

    // Set velocity for visual rotation
    this.vx = (targetX - this.x) / Math.max(dt, 0.001);
    this.vy = (targetY - this.y) / Math.max(dt, 0.001);

    // Drift hover center slightly
    this.hoverCenterX += (Math.random() - 0.5) * 20 * dt;
    this.hoverCenterY += (Math.random() - 0.5) * 20 * dt;
  }

  _updateDash(dt) {
    // Dash: maintain velocity toward target
    const dx = this.dashTargetX - this.x;
    const dy = this.dashTargetY - this.y;
    const dist = Math.sqrt(dx * dx + dy * dy);

    if (dist < 10 || this.modeTimer <= 0.05) {
      // Close to target or about to switch mode: slow down and transition
      this.vx *= 0.9;
      this.vy *= 0.9;
    } else {
      // Accelerate toward target
      const speed = this.baseSpeed * 2.5 * this.speedMultiplier;
      this.vx += (dx / dist) * speed * 3 * dt;
      this.vy += (dy / dist) * speed * 3 * dt;
      // Clamp
      const currentSpeed = Math.sqrt(this.vx * this.vx + this.vy * this.vy);
      if (currentSpeed > speed) {
        this.vx = (this.vx / currentSpeed) * speed;
        this.vy = (this.vy / currentSpeed) * speed;
      }
    }

    this.x += this.vx * dt;
    this.y += this.vy * dt;
  }

  _updateBrownian(dt) {
    // Brownian: change direction frequently, random walk
    this.brownianChangeTimer -= dt;
    if (this.brownianChangeTimer <= 0) {
      // Random direction change
      this.brownianDir += (Math.random() - 0.5) * Math.PI * 0.8;
      this.brownianChangeTimer = 0.15 + Math.random() * 0.4;
    }

    const speed = this.baseSpeed * 0.85 * this.speedMultiplier;
    this.vx += Math.cos(this.brownianDir) * speed * 2 * dt;
    this.vy += Math.sin(this.brownianDir) * speed * 2 * dt;

    // Damping to prevent infinite acceleration
    const currentSpeed = Math.sqrt(this.vx * this.vx + this.vy * this.vy);
    if (currentSpeed > speed * 1.2) {
      const damp = 0.95;
      this.vx *= damp;
      this.vy *= damp;
    }

    this.x += this.vx * dt;
    this.y += this.vy * dt;
  }

  _handleBoundaries() {
    const margin = this.radius;
    let bounced = false;

    if (this.x < margin) {
      this.x = margin;
      this.vx = Math.abs(this.vx) * (0.6 + Math.random() * 0.4);
      bounced = true;
    } else if (this.x > GAME_W - margin) {
      this.x = GAME_W - margin;
      this.vx = -Math.abs(this.vx) * (0.6 + Math.random() * 0.4);
      bounced = true;
    }

    if (this.y < margin) {
      this.y = margin;
      this.vy = Math.abs(this.vy) * (0.6 + Math.random() * 0.4);
      bounced = true;
    } else if (this.y > GAME_H - margin) {
      this.y = GAME_H - margin;
      this.vy = -Math.abs(this.vy) * (0.6 + Math.random() * 0.4);
      bounced = true;
    }

    // On bounce, add some random angular deflection
    if (bounced) {
      const deflectAngle = (Math.random() - 0.5) * Math.PI * 0.6;
      const speed = Math.sqrt(this.vx * this.vx + this.vy * this.vy);
      const currentAngle = Math.atan2(this.vy, this.vx);
      const newAngle = currentAngle + deflectAngle;
      this.vx = Math.cos(newAngle) * speed;
      this.vy = Math.sin(newAngle) * speed;
    }
  }

  /**
   * Check if a point (px, py) hits this mosquito
   * @param {number} px
   * @param {number} py
   * @param {number} hitRadiusMultiplier - 1.0 = exact, 1.2 = generous
   */
  hitTest(px, py, hitRadiusMultiplier = 1.0) {
    const dx = this.x - px;
    const dy = this.y - py;
    const dist = Math.sqrt(dx * dx + dy * dy);
    return dist < this.radius * hitRadiusMultiplier;
  }

  /**
   * Check if a line segment (line from ax,ay to bx,by) intersects this mosquito
   */
  lineHitTest(ax, ay, bx, by, radiusMultiplier = 1.2) {
    // Closest point on segment to circle center
    const dx = bx - ax;
    const dy = by - ay;
    const lenSq = dx * dx + dy * dy;

    if (lenSq === 0) {
      // Degenerate to point test
      return this.hitTest(ax, ay, radiusMultiplier);
    }

    let t = ((this.x - ax) * dx + (this.y - ay) * dy) / lenSq;
    t = Math.max(0, Math.min(1, t));

    const closestX = ax + t * dx;
    const closestY = ay + t * dy;

    const distX = this.x - closestX;
    const distY = this.y - closestY;
    const dist = Math.sqrt(distX * distX + distY * distY);

    return dist < this.radius * radiusMultiplier;
  }

  /**
   * Check if this mosquito is inside a repulsion field
   * @returns {object|null} - {dx, dy, dist} or null
   */
  isInRepulsionField(fieldX, fieldY, fieldRadius) {
    const dx = this.x - fieldX;
    const dy = this.y - fieldY;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < fieldRadius) {
      return { dx, dy, dist };
    }
    return null;
  }

  /**
   * Apply repulsion force (from mosquito coil)
   */
  applyRepulsion(fx, fy, strength) {
    this.vx += fx * strength;
    this.vy += fy * strength;
    // Also nudge position
    this.x += fx * strength * 2;
    this.y += fy * strength * 2;
    // If in hover mode, update hover center to prevent snapping back
    if (this.mode === FLIGHT_MODES.HOVER) {
      this.hoverCenterX += fx * strength * 4;
      this.hoverCenterY += fy * strength * 4;
    }
    // Switch to brownian briefly after strong repulsion
    if (strength > 0.5 && Math.random() < 0.3) {
      this._enterMode(FLIGHT_MODES.BROWNIAN);
    }
  }

  /**
   * Spawn at a random edge position
   */
  spawn() {
    this.alive = true;
    // Spawn from a random edge
    const edge = Math.floor(Math.random() * 4);
    switch (edge) {
      case 0: this.x = Math.random() * GAME_W; this.y = -20; break;         // top
      case 1: this.x = GAME_W + 20; this.y = Math.random() * GAME_H; break; // right
      case 2: this.x = Math.random() * GAME_W; this.y = GAME_H + 20; break; // bottom
      case 3: this.x = -20; this.y = Math.random() * GAME_H; break;         // left
    }
    this.vx = 0;
    this.vy = 0;
    this.rotation = Math.random() * Math.PI * 2;
    this.agitation = 0;
    this._enterMode(this._randomMode());
    this.callCooldown = 1 + Math.random() * 3;
  }
}

/**
 * Update the game area dimensions (called on resize)
 */
function setGameArea(w, h) {
  GAME_W = w;
  GAME_H = h;
}
