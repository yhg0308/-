/**
 * Leaderboard — LocalStorage CRUD, fuzzy search, duplicate name handling
 *
 * Storage key: 'mosquito-slayer-leaderboard'
 * Schema: [{ name: string, kills: number, time: string (ISO), date: string }]
 */

const STORAGE_KEY = 'mosquito-slayer-leaderboard';

class Leaderboard {
  constructor() {
    this.entries = [];
    this.load();
  }

  /**
   * Load from LocalStorage
   */
  load() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      this.entries = raw ? JSON.parse(raw) : [];
      // Validate
      if (!Array.isArray(this.entries)) this.entries = [];
    } catch (e) {
      this.entries = [];
    }
  }

  /**
   * Save to LocalStorage
   */
  save() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.entries));
    } catch (e) {
      console.warn('Failed to save leaderboard:', e);
    }
  }

  /**
   * Check if a nickname exists
   */
  nameExists(name) {
    return this.entries.some(e => e.name.toLowerCase() === name.toLowerCase());
  }

  /**
   * Find entry by exact name (case-insensitive)
   */
  findByName(name) {
    return this.entries.find(e => e.name.toLowerCase() === name.toLowerCase());
  }

  /**
   * Add or overwrite an entry
   * @param {string} name
   * @param {number} kills
   * @param {boolean} overwrite - if true, overwrite existing entry even with lower kills
   * @returns {{success: boolean, error?: string}}
   */
  addEntry(name, kills, overwrite = false) {
    const trimmed = name.trim();
    if (!trimmed) {
      return { success: false, error: '昵称不能为空' };
    }
    if (trimmed.length > 20) {
      return { success: false, error: '昵称不能超过20个字符' };
    }

    const existingIndex = this.entries.findIndex(
      e => e.name.toLowerCase() === trimmed.toLowerCase()
    );

    const now = new Date();
    const entry = {
      name: trimmed,
      kills,
      time: now.toISOString(),
      date: now.toLocaleString('zh-CN', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
      }),
    };

    if (existingIndex >= 0) {
      if (!overwrite) {
        // Can only overwrite if new kills are higher
        if (kills <= this.entries[existingIndex].kills) {
          return {
            success: false,
            error: 'duplicate',
            existingEntry: this.entries[existingIndex],
          };
        }
      }
      // Overwrite
      this.entries[existingIndex] = entry;
    } else {
      this.entries.push(entry);
    }

    // Sort by kills descending
    this.entries.sort((a, b) => b.kills - a.kills);

    this.save();
    return { success: true, entry };
  }

  /**
   * Get all entries, optionally filtered by search query
   * @param {string} query - Fuzzy search string
   * @returns {Array}
   */
  getEntries(query = '') {
    if (!query.trim()) {
      return this.entries.map((e, i) => ({ ...e, rank: i + 1 }));
    }

    const q = query.trim().toLowerCase();
    // Fuzzy match: check if query characters appear in order
    const filtered = this.entries.filter(e => {
      const name = e.name.toLowerCase();
      // Simple contains match + character sequence match
      if (name.includes(q)) return true;
      // Check if all characters of q appear in name in order (fuzzy)
      let qi = 0;
      for (let ni = 0; ni < name.length && qi < q.length; ni++) {
        if (name[ni] === q[qi]) qi++;
      }
      return qi === q.length;
    });

    return filtered.map((e, i) => ({ ...e, rank: i + 1 }));
  }

  /**
   * Delete all entries (for debugging)
   */
  clearAll() {
    this.entries = [];
    this.save();
  }
}

// Singleton
const LB = new Leaderboard();
