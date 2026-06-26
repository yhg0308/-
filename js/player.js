/**
 * Player State — Stamina, kills, game progress
 */
class PlayerState {
  constructor() {
    this.reset();
  }

  reset() {
    // Stamina
    this.maxStamina = 100;
    this.stamina = this.maxStamina;

    // Stamina regen: +1 every 0.3 seconds (fast recovery)
    this.regenInterval = 0.3;
    this.regenTimer = 0;

    // Stamina depleted cooldown: 3 seconds of helplessness
    this.depletedCooldown = 3.0;
    this.depletedTimer = 0;
    this.isDepleted = false;

    // Kill count
    this.kills = 0;

    // Miss click counter (for tracking -1 penalty)
    this.missClicks = 0;

    // Unlocked tools: 0=hand, 1=flyswatter, 2=electric, 3=coil
    this.unlockedTools = [true, false, false, false];
  }

  /**
   * Check unlock conditions based on kills
   */
  checkUnlocks() {
    if (this.kills >= 10 && !this.unlockedTools[1]) {
      this.unlockedTools[1] = true;
      return { tool: 1, name: '苍蝇拍' };
    }
    if (this.kills >= 50 && !this.unlockedTools[2]) {
      this.unlockedTools[2] = true;
      return { tool: 2, name: '电蚊拍' };
    }
    if (this.kills >= 200 && !this.unlockedTools[3]) {
      this.unlockedTools[3] = true;
      return { tool: 3, name: '蚊香' };
    }
    return null;
  }

  /**
   * Get kills remaining until next unlock
   */
  getNextUnlock() {
    if (!this.unlockedTools[1]) return { tool: 1, name: '苍蝇拍', needed: 10 - this.kills };
    if (!this.unlockedTools[2]) return { tool: 2, name: '电蚊拍', needed: 50 - this.kills };
    if (!this.unlockedTools[3]) return { tool: 3, name: '蚊香', needed: 200 - this.kills };
    return null;
  }

  /**
   * Try to spend stamina. Returns true if successful.
   * @param {number} amount - Stamina cost
   * @returns {boolean}
   */
  spendStamina(amount) {
    if (this.isDepleted) return false;
    if (this.stamina < amount) return false;

    this.stamina -= amount;
    if (this.stamina <= 0) {
      this.stamina = 0;
      this.isDepleted = true;
      this.depletedTimer = this.depletedCooldown;
    }
    return true;
  }

  /**
   * Register a miss click (empty area click)
   */
  missClick() {
    if (this.isDepleted) return false;
    this.stamina -= 1;
    this.missClicks++;
    if (this.stamina <= 0) {
      this.stamina = 0;
      this.isDepleted = true;
      this.depletedTimer = this.depletedCooldown;
    }
    return true;
  }

  /**
   * Add a kill
   */
  addKill() {
    this.kills++;
  }

  /**
   * Get stamina percentage (0-1)
   */
  getStaminaPercent() {
    return this.stamina / this.maxStamina;
  }

  /**
   * Get stamina color class
   */
  getStaminaClass() {
    const pct = this.getStaminaPercent();
    if (pct > 0.5) return '';
    if (pct > 0.2) return 'warning';
    return 'danger';
  }

  /**
   * Get difficulty speed bonus (0 = normal, up to 0.5 at 0 stamina)
   */
  getSpeedBonus() {
    if (this.isDepleted) return 0.5;
    const pct = this.getStaminaPercent();
    return (1 - pct) * 0.4; // max 0.4 when stamina is very low
  }

  /**
   * Get max coil count based on kills
   */
  getMaxCoils() {
    // Base 1, +1 every 500 kills after 200, max 5
    if (this.kills < 200) return this.unlockedTools[3] ? 1 : 0;
    const extra = Math.floor((this.kills - 200) / 500) + 1;
    return Math.min(5, extra);
  }

  /**
   * Update per-frame
   * @param {number} dt - Delta time in seconds
   */
  update(dt) {
    // Stamina regen
    if (!this.isDepleted) {
      this.regenTimer += dt;
      while (this.regenTimer >= this.regenInterval) {
        this.regenTimer -= this.regenInterval;
        this.stamina = Math.min(this.maxStamina, this.stamina + 1);
      }
    } else {
      // Depleted cooldown
      this.depletedTimer -= dt;
      if (this.depletedTimer <= 0) {
        this.isDepleted = false;
        this.stamina = 5; // start with a little to prevent instant re-depletion
      }
    }
  }
}

// Singleton
const Player = new PlayerState();
