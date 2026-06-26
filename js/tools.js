/**
 * Tool System — Hand, Flyswatter, Electric Swatter, Mosquito Coil
 *
 * Tool types:
 *   0: HAND      — precise click, 120% hit radius
 *   1: FLYSWATTER — 2x fan area, screen shake
 *   2: ELECTRIC  — drag-to-kill, arc VFX
 *   3: COIL      — placement mode, 150px repulsion field
 */

const TOOLS = {
  HAND: 0,
  FLYSWATTER: 1,
  ELECTRIC: 2,
  COIL: 3
};

const TOOL_CONFIG = {
  [TOOLS.HAND]: {
    name: '手',
    icon: '🖐️',
    cost: 0,
    hitRadiusMultiplier: 1.2,
    cooldown: 0.15,
  },
  [TOOLS.FLYSWATTER]: {
    name: '苍蝇拍',
    icon: '🪰',
    cost: 0,
    hitRadiusMultiplier: 2.0,
    cooldown: 0.25,
    screenShake: true,
  },
  [TOOLS.ELECTRIC]: {
    name: '电蚊拍',
    icon: '⚡',
    cost: 1,
    cooldown: 0.3,
    isDrag: true,
  },
  [TOOLS.COIL]: {
    name: '蚊香',
    icon: '🪴',
    cost: 10,
    cooldown: 0.5,
    isPlacement: true,
  },
};

class ToolSystem {
  constructor() {
    this.currentTool = TOOLS.HAND;
    this.cooldownTimer = 0;

    // Electric swatter drag state
    this.isDragging = false;
    this.dragStartX = 0;
    this.dragStartY = 0;
    this.dragCurrentX = 0;
    this.dragCurrentY = 0;
    this.dragPath = []; // array of {x, y} for collision detection

    // Coil placement state
    this.isPlacingCoil = false;

    // Placed coils
    this.coils = []; // [{x, y, placedAt}]

    // Tool unlock status (synced from Player)
    this.unlockedTools = [true, false, false, false];

    // Visual: screen shake
    this.shakeAmount = 0;
    this.shakeDuration = 0;

    // For hit VFX tracking (consumed by main.js render)
    this.lastHitVFX = null; // {type: 'slap'|'zap', x, y, time}
  }

  reset() {
    this.currentTool = TOOLS.HAND;
    this.cooldownTimer = 0;
    this.isDragging = false;
    this.dragPath = [];
    this.isPlacingCoil = false;
    this.coils = [];
    this.shakeAmount = 0;
    this.shakeDuration = 0;
    this.lastHitVFX = null;
  }

  /**
   * Switch to a tool. Returns true if successful.
   */
  switchTool(toolIndex) {
    if (toolIndex >= 0 && toolIndex <= 3 && this.unlockedTools[toolIndex]) {
      // Cancel any placement mode
      this.isPlacingCoil = false;
      this.currentTool = toolIndex;
      return true;
    }
    return false;
  }

  /**
   * Enter coil placement mode
   */
  startCoilPlacement() {
    if (!this.unlockedTools[TOOLS.COIL]) return false;
    const maxCoils = Player.getMaxCoils();
    if (this.coils.length >= maxCoils) return false;
    if (Player.stamina < TOOL_CONFIG[TOOLS.COIL].cost) return false;

    this.currentTool = TOOLS.COIL;
    this.isPlacingCoil = true;
    return true;
  }

  /**
   * Place a coil at (x, y) in game coordinates
   */
  placeCoil(x, y) {
    if (!this.isPlacingCoil) return false;
    const maxCoils = Player.getMaxCoils();
    if (this.coils.length >= maxCoils) return false;
    if (!Player.spendStamina(TOOL_CONFIG[TOOLS.COIL].cost)) return false;

    this.coils.push({ x, y, placedAt: Date.now() });
    this.isPlacingCoil = false;
    return true;
  }

  /**
   * Recycle (remove) a placed coil
   */
  recycleCoil(index) {
    if (index < 0 || index >= this.coils.length) return false;
    if (!Player.spendStamina(5)) return false; // costs 5 stamina to recycle

    this.coils.splice(index, 1);
    return true;
  }

  /**
   * Find a coil near (x, y) for recycling
   * @returns {number} index of coil, or -1
   */
  findCoilAt(x, y) {
    for (let i = this.coils.length - 1; i >= 0; i--) {
      const dx = this.coils[i].x - x;
      const dy = this.coils[i].y - y;
      if (Math.sqrt(dx * dx + dy * dy) < 30) {
        return i;
      }
    }
    return -1;
  }

  /**
   * Start electric swatter drag
   */
  startDrag(x, y) {
    if (this.currentTool !== TOOLS.ELECTRIC) return;
    if (!Player.spendStamina(TOOL_CONFIG[TOOLS.ELECTRIC].cost)) return;
    this.isDragging = true;
    this.dragStartX = x;
    this.dragStartY = y;
    this.dragCurrentX = x;
    this.dragCurrentY = y;
    this.dragPath = [{ x, y }];
  }

  /**
   * Update drag position
   */
  updateDrag(x, y) {
    if (!this.isDragging) return;
    this.dragCurrentX = x;
    this.dragCurrentY = y;
    this.dragPath.push({ x, y });
    // Keep path manageable
    if (this.dragPath.length > 200) {
      this.dragPath = this.dragPath.slice(-100);
    }
  }

  /**
   * End drag, return the path for collision checking
   */
  endDrag() {
    if (!this.isDragging) return [];
    this.isDragging = false;
    const path = [...this.dragPath];
    this.dragPath = [];
    return path;
  }

  /**
   * Handle a click attack with the current tool (hand or flyswatter)
   * @returns {object} {hit: bool, x, y, hitMosquitoes: []}
   */
  /**
   * Check if a click attack is possible (cooldown, depletion, stamina)
   * Does NOT deduct stamina — caller must do that after hit/miss determination.
   * @returns {object|null} config object if click allowed, null if blocked
   */
  canClick() {
    if (this.currentTool === TOOLS.ELECTRIC) return null;
    if (this.currentTool === TOOLS.COIL) return null;
    if (this.cooldownTimer > 0) return null;
    if (Player.isDepleted) return null;

    const config = TOOL_CONFIG[this.currentTool];
    if (Player.stamina < config.cost) return null;

    this.cooldownTimer = config.cooldown;
    return {
      cost: config.cost,
      hitRadiusMultiplier: config.hitRadiusMultiplier,
      type: this.currentTool === TOOLS.FLYSWATTER ? 'swatter' : 'hand',
    };
  }

  /**
   * Get repulsion fields (all placed coils)
   */
  getRepulsionFields() {
    return this.coils.map(c => ({
      x: c.x,
      y: c.y,
      radius: 150,
    }));
  }

  /**
   * Update per-frame
   */
  update(dt) {
    if (this.cooldownTimer > 0) {
      this.cooldownTimer -= dt;
    }

    // Screen shake decay
    if (this.shakeDuration > 0) {
      this.shakeDuration -= dt;
      if (this.shakeDuration <= 0) {
        this.shakeAmount = 0;
      }
    }
  }

  /**
   * Trigger screen shake
   */
  triggerShake() {
    this.shakeAmount = 8;
    this.shakeDuration = 0.1;
  }

  /**
   * Get current screen shake offset
   */
  getShakeOffset() {
    if (this.shakeDuration <= 0) return { x: 0, y: 0 };
    const intensity = this.shakeAmount * (this.shakeDuration / 0.1);
    return {
      x: (Math.random() - 0.5) * intensity * 2,
      y: (Math.random() - 0.5) * intensity * 2,
    };
  }
}

// Singleton
const Tools = new ToolSystem();
