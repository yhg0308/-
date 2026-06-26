/**
 * Main Entry — Game loop, state machine, canvas rendering, input handling
 */

// ======================== DOM REFS ========================
const $loading = document.getElementById('loading-screen');
const $loadingBar = document.getElementById('loading-bar');
const $loadingText = document.getElementById('loading-text');
const $menu = document.getElementById('menu-screen');
const $game = document.getElementById('game-screen');
const $leaderboard = document.getElementById('leaderboard-screen');
const $canvas = document.getElementById('game-canvas');
const ctx = $canvas.getContext('2d');

const $staminaFill = document.getElementById('stamina-bar-fill');
const $staminaText = document.getElementById('stamina-text');
const $killCount = document.getElementById('kill-count-text');
const $coilPlaced = document.getElementById('coil-placed-text');
const $coilMax = document.getElementById('coil-max-text');
const $toolSlots = [
  document.getElementById('tool-slot-0'),
  document.getElementById('tool-slot-1'),
  document.getElementById('tool-slot-2'),
  document.getElementById('tool-slot-3'),
];
const $newbieHint = document.getElementById('newbie-hint');
const $hintKillsRemaining = document.getElementById('hint-kills-remaining');
const $modalOverlay = document.getElementById('modal-overlay');
const $modalBox = document.getElementById('modal-box');
const $duplicateModal = document.getElementById('duplicate-modal');
const $leaderboardTbody = document.getElementById('leaderboard-tbody');
const $leaderboardEmpty = document.getElementById('leaderboard-empty');
const $searchInput = document.getElementById('search-input');
const $searchNoResult = document.getElementById('search-no-result');
const $electricCursor = document.getElementById('electric-cursor');
const $coilIndicator = document.getElementById('coil-placement-indicator');

// ======================== GAME STATE ========================
const STATE = {
  LOADING: 'loading',
  MENU: 'menu',
  PLAYING: 'playing',
  PAUSED: 'paused',
  LEADERBOARD: 'leaderboard',
};

let currentState = STATE.LOADING;
let mosquitoes = [];
const MOSQUITO_COUNT = 20;

// Mosquito sprite image
let mosquitoImg = null;
let flyswatterImg = null;
let electricNormalImg = null;
let electricZappingImg = null;
let coilImg = null;

// Mouse tracking
let mouseX = -1000;
let mouseY = -1000;
let mouseOnCanvas = false;

// Frame timing
let lastTime = 0;
let animFrameId = null;

// Mosquito call scheduling
let mosquitoCallIndex = 0;

// Swatter swish visual timer
let swatterSwish = null; // {x, y, time, maxTime}

// Trailing effect for electric drag
let electricTrail = [];

// Selected tool index
let selectedToolIndex = 0;

// For the end-game flow
let pendingEndGame = false;

// Loading progress
let assetsToLoad = 0;
let assetsLoaded = 0;

// ======================== ASSET LOADING ========================
function preloadAssets() {
  const assets = [
    { key: 'mosquito', src: 'assets/蚊子照片.png' },
    { key: 'flyswatter', src: 'assets/苍蝇拍.png' },
    { key: 'electricNormal', src: 'assets/电蚊拍（常态）.png' },
    { key: 'electricZapping', src: 'assets/电蚊拍（电到蚊子的状态）.png' },
    { key: 'coil', src: 'assets/蚊香.png' },
  ];

  assetsToLoad = assets.length;
  assetsLoaded = 0;

  const imageMap = {};
  return Promise.all(assets.map(a => {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => {
        assetsLoaded++;
        $loadingBar.style.width = `${(assetsLoaded / assetsToLoad) * 100}%`;
        $loadingText.textContent = `正在加载素材... (${assetsLoaded}/${assetsToLoad})`;
        imageMap[a.key] = img;
        resolve();
      };
      img.onerror = () => {
        // Continue even if image fails
        assetsLoaded++;
        $loadingBar.style.width = `${(assetsLoaded / assetsToLoad) * 100}%`;
        imageMap[a.key] = null;
        console.warn(`Failed to load: ${a.src}`);
        resolve();
      };
      img.src = a.src;
    });
  })).then(() => imageMap);
}

// ======================== SCREEN MANAGEMENT ========================
function showScreen(screenId) {
  // Hide all screens
  const allScreens = [$menu, $game, $leaderboard].filter(Boolean);
  allScreens.forEach(s => s.classList.add('hidden'));
  // Show target screen (skip if it's already in the hide list to avoid add-then-remove race)
  const screen = document.getElementById(screenId);
  if (screen) {
    // Force display via inline style as fallback
    screen.classList.remove('hidden');
    screen.style.display = '';
  }
}

function setState(newState) {
  currentState = newState;
}

// ======================== GAME INIT / RESET ========================
function initGame() {
  Player.reset();
  Tools.reset();
  Particles.reset();
  mosquitoes = [];
  electricTrail = [];
  swatterSwish = null;
  selectedToolIndex = 0;
  pendingEndGame = false;

  // Sync tools to player unlocks
  Tools.unlockedTools = [...Player.unlockedTools];

  // Create mosquitoes
  for (let i = 0; i < MOSQUITO_COUNT; i++) {
    const m = new Mosquito(i);
    mosquitoes.push(m);
  }

  // Set up audio panners
  Audio.setupMosquitoPanners(MOSQUITO_COUNT);
  Audio.startAmbientBuzz();

  // Update HUD
  updateHUD();
  updateToolBar();

  // Reset mouse
  mouseX = -1000;
  mouseY = -1000;
}

// ======================== GAME LOOP ========================
function gameLoop(timestamp) {
  animFrameId = requestAnimationFrame(gameLoop);

  if (currentState !== STATE.PLAYING && currentState !== STATE.PAUSED) {
    lastTime = timestamp;
    return;
  }

  // Calculate delta time
  let dt = (timestamp - lastTime) / 1000;
  if (dt <= 0) dt = 0.016;
  if (dt > 0.1) dt = 0.1; // cap to prevent physics explosion on tab switch
  lastTime = timestamp;

  if (currentState === STATE.PLAYING) {
    update(dt);
  }
  render();
}

function update(dt) {
  // Update player (stamina regen, depleted timer)
  Player.update(dt);

  // Update tools
  Tools.update(dt);

  // Sync unlocks
  Tools.unlockedTools = [...Player.unlockedTools];
  const unlock = Player.checkUnlocks();
  if (unlock) {
    updateToolBar();
    flashToolSlot(unlock.tool);
    // Show celebration hint
    $newbieHint.classList.remove('hidden');
    const bubble = $newbieHint.querySelector('.hint-bubble');
    if (bubble) {
      bubble.textContent = `🎉 已解锁${unlock.name}！按数字键${unlock.tool + 1}快速切换`;
      setTimeout(() => {
        $newbieHint.classList.add('hidden');
        bubble.textContent = '';
      }, 3000);
    }
  }

  // Update mosquitoes
  const speedBonus = Player.getSpeedBonus();
  for (const m of mosquitoes) {
    if (m.alive) {
      m.update(dt, mouseX, mouseY, speedBonus);

      // Check coil repulsion
      const fields = Tools.getRepulsionFields();
      for (const field of fields) {
        const rep = m.isInRepulsionField(field.x, field.y, field.radius);
        if (rep) {
          const nx = rep.dx / Math.max(rep.dist, 1);
          const ny = rep.dy / Math.max(rep.dist, 1);
          const strength = (1 - rep.dist / field.radius) * 3 * dt;
          m.applyRepulsion(nx, ny, strength);
        }
      }

      // Audio call
      if (m.callCooldown <= 0 && Math.random() < 0.02) {
        Audio.playMosquitoCall(m.index);
        m.callCooldown = 2 + Math.random() * 8;
      }
    }
  }

  // Update particles
  Particles.update(dt);

  // Coil smoke emission
  for (const coil of Tools.coils) {
    if (Math.random() < 0.3) {
      Particles.emitSmoke(
        coil.x + (Math.random() - 0.5) * 20,
        coil.y - 10 + (Math.random() - 0.5) * 10
      );
    }
  }

  // Swatter swish decay
  if (swatterSwish) {
    swatterSwish.time -= dt;
    if (swatterSwish.time <= 0) swatterSwish = null;
  }

  // Electric trail fade
  if (electricTrail.length > 0) {
    electricTrail = electricTrail.filter(t => {
      t.life -= dt;
      return t.life > 0;
    });
  }

  // Update HUD
  updateHUD();

  // Newbie hint logic
  updateNewbieHint();
}

function render() {
  const w = $canvas.width;
  const h = $canvas.height;

  // Clear
  ctx.clearRect(0, 0, w, h);

  // Screen shake offset
  const shake = Tools.getShakeOffset();

  ctx.save();
  ctx.translate(shake.x, shake.y);

  // Draw placed coils
  drawCoils();

  // Draw mosquitoes
  drawMosquitoes();

  // Draw particles (under UI)
  Particles.render(ctx);

  // Draw swatter swish
  if (swatterSwish) {
    drawSwatterSwish();
  }

  // Draw electric trail
  if (electricTrail.length > 0) {
    drawElectricTrail();
  }

  // Draw electric drag preview
  if (Tools.isDragging) {
    drawDragPreview();
  }

  // Draw coil placement indicator (rendered on canvas as well)
  if (Tools.isPlacingCoil && mouseOnCanvas) {
    drawPlacementIndicator();
  }

  ctx.restore();

  // Draw custom cursor
  drawCursor();
}

// ======================== RENDER HELPERS ========================
function drawMosquitoes() {
  for (const m of mosquitoes) {
    if (!m.alive) continue;

    ctx.save();
    ctx.translate(m.x, m.y);
    ctx.rotate(m.rotation);

    if (mosquitoImg && mosquitoImg.complete && mosquitoImg.naturalWidth > 0) {
      // Use photo sprite
      const size = m.radius * 2.8;
      // Wing flutter effect — slight scale oscillation
      const wingScale = 1 + Math.sin(m.wingPhase) * 0.08;
      ctx.scale(wingScale, wingScale);
      ctx.drawImage(mosquitoImg, -size / 2, -size / 2, size, size);
    } else {
      // Fallback: draw a simple mosquito shape
      drawFallbackMosquito(m);
    }

    ctx.restore();

    // Agitation indicator (sweat drops when agitated)
    if (m.agitation > 0.5) {
      ctx.save();
      ctx.globalAlpha = m.agitation * 0.6;
      ctx.fillStyle = '#88ccff';
      ctx.font = `${8 + m.agitation * 4}px sans-serif`;
      ctx.fillText('💧', m.x + m.radius, m.y - m.radius);
      ctx.restore();
    }
  }
}

function drawFallbackMosquito(m) {
  // Simple mosquito silhouette
  const s = m.radius; // scale factor

  // Body
  ctx.fillStyle = '#333';
  ctx.beginPath();
  ctx.ellipse(0, 0, s * 1.2, s * 0.4, 0, 0, Math.PI * 2);
  ctx.fill();

  // Wings (fluttering)
  const wingAngle = Math.sin(m.wingPhase) * 0.5;
  ctx.fillStyle = 'rgba(200,200,220,0.5)';
  ctx.beginPath();
  ctx.ellipse(-s * 0.3, -s * 0.5, s * 0.8, s * 0.3, -0.3 + wingAngle, 0, Math.PI * 2);
  ctx.fill();
  ctx.beginPath();
  ctx.ellipse(s * 0.3, -s * 0.5, s * 0.8, s * 0.3, 0.3 - wingAngle, 0, Math.PI * 2);
  ctx.fill();

  // Legs
  ctx.strokeStyle = '#555';
  ctx.lineWidth = 1;
  for (let i = -1; i <= 1; i += 0.5) {
    ctx.beginPath();
    ctx.moveTo(i * s * 0.4, s * 0.2);
    ctx.lineTo(i * s * 1.2, s * 0.8);
    ctx.stroke();
  }
}

function drawCoils() {
  for (const coil of Tools.coils) {
    ctx.save();
    ctx.translate(coil.x, coil.y);

    if (coilImg && coilImg.complete && coilImg.naturalWidth > 0) {
      ctx.drawImage(coilImg, -25, -25, 50, 50);
    } else {
      // Fallback
      ctx.fillStyle = '#8bc34a';
      ctx.beginPath();
      ctx.arc(0, 0, 12, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#d4e157';
      ctx.beginPath();
      ctx.arc(0, 0, 6, 0, Math.PI * 2);
      ctx.fill();
    }

    // Repulsion field visualization (faint)
    ctx.strokeStyle = 'rgba(139, 195, 74, 0.08)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(0, 0, 150, 0, Math.PI * 2);
    ctx.stroke();
    ctx.fillStyle = 'rgba(139, 195, 74, 0.03)';
    ctx.fill();

    ctx.restore();
  }
}

function drawSwatterSwish() {
  const { x, y, time, maxTime } = swatterSwish;
  const alpha = time / maxTime;
  ctx.save();
  ctx.globalAlpha = alpha * 0.5;
  ctx.fillStyle = '#ffd700';
  ctx.beginPath();
  ctx.arc(x, y, 30 * (2 - alpha), 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function drawElectricTrail() {
  for (const t of electricTrail) {
    ctx.save();
    ctx.globalAlpha = t.life * 0.8;
    ctx.strokeStyle = '#88ccff';
    ctx.lineWidth = 2 + t.life * 3;
    ctx.shadowColor = '#4488ff';
    ctx.shadowBlur = 8;
    ctx.beginPath();
    // Small squiggle
    const len = 8;
    const midX = (t.x1 + t.x2) / 2 + (Math.random() - 0.5) * 4;
    const midY = (t.y1 + t.y2) / 2 + (Math.random() - 0.5) * 4;
    ctx.moveTo(t.x1, t.y1);
    ctx.quadraticCurveTo(midX, midY, t.x2, t.y2);
    ctx.stroke();
    ctx.restore();
  }
}

function drawDragPreview() {
  if (Tools.dragPath.length < 2) return;
  ctx.save();
  ctx.strokeStyle = 'rgba(100, 180, 255, 0.7)';
  ctx.lineWidth = 4;
  ctx.shadowColor = '#4488ff';
  ctx.shadowBlur = 12;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.beginPath();
  ctx.moveTo(Tools.dragPath[0].x, Tools.dragPath[0].y);
  for (let i = 1; i < Tools.dragPath.length; i++) {
    ctx.lineTo(Tools.dragPath[i].x, Tools.dragPath[i].y);
  }
  ctx.stroke();
  ctx.restore();
}

function drawPlacementIndicator() {
  ctx.save();
  ctx.strokeStyle = 'rgba(139, 195, 74, 0.7)';
  ctx.lineWidth = 2;
  ctx.setLineDash([8, 4]);
  ctx.beginPath();
  ctx.arc(mouseX, mouseY, 150, 0, Math.PI * 2);
  ctx.stroke();
  ctx.setLineDash([]);

  // Crosshair
  ctx.strokeStyle = 'rgba(139, 195, 74, 0.5)';
  ctx.beginPath();
  ctx.moveTo(mouseX - 20, mouseY);
  ctx.lineTo(mouseX + 20, mouseY);
  ctx.moveTo(mouseX, mouseY - 20);
  ctx.lineTo(mouseX, mouseY + 20);
  ctx.stroke();
  ctx.restore();
}

function drawCursor() {
  if (!mouseOnCanvas) return;

  // In placement mode, crosshair is rendered by drawPlacementIndicator()
  if (Tools.isPlacingCoil) return;

  // Electric drag cursor is handled by DOM element
  if (Tools.isDragging) return;

  ctx.save();

  const tool = Tools.currentTool;
  let hitRadius = 14;

  if (tool === TOOLS.HAND) {
    // Crosshair cursor
    const s = 14;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.85)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(mouseX - s, mouseY);
    ctx.lineTo(mouseX - s / 4, mouseY);
    ctx.moveTo(mouseX + s / 4, mouseY);
    ctx.lineTo(mouseX + s, mouseY);
    ctx.moveTo(mouseX, mouseY - s);
    ctx.lineTo(mouseX, mouseY - s / 4);
    ctx.moveTo(mouseX, mouseY + s / 4);
    ctx.lineTo(mouseX, mouseY + s);
    ctx.stroke();

    // Center dot
    ctx.fillStyle = '#fff';
    ctx.beginPath();
    ctx.arc(mouseX, mouseY, 3.5, 0, Math.PI * 2);
    ctx.fill();

    // Hit radius ring
    hitRadius = 14 * TOOL_CONFIG[TOOLS.HAND].hitRadiusMultiplier;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(mouseX, mouseY, hitRadius, 0, Math.PI * 2);
    ctx.stroke();
  } else if (tool === TOOLS.FLYSWATTER) {
    // Larger golden crosshair for swatter
    const s = 18;
    ctx.strokeStyle = 'rgba(255, 215, 0, 0.85)';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(mouseX - s, mouseY);
    ctx.lineTo(mouseX - s / 4, mouseY);
    ctx.moveTo(mouseX + s / 4, mouseY);
    ctx.lineTo(mouseX + s, mouseY);
    ctx.moveTo(mouseX, mouseY - s);
    ctx.lineTo(mouseX, mouseY - s / 4);
    ctx.moveTo(mouseX, mouseY + s / 4);
    ctx.lineTo(mouseX, mouseY + s);
    ctx.stroke();

    // Center diamond
    ctx.fillStyle = '#ffd700';
    ctx.beginPath();
    ctx.moveTo(mouseX, mouseY - 5);
    ctx.lineTo(mouseX + 5, mouseY);
    ctx.lineTo(mouseX, mouseY + 5);
    ctx.lineTo(mouseX - 5, mouseY);
    ctx.closePath();
    ctx.fill();

    // Larger fan hit area
    hitRadius = 14 * TOOL_CONFIG[TOOLS.FLYSWATTER].hitRadiusMultiplier;
    ctx.strokeStyle = 'rgba(255, 215, 0, 0.25)';
    ctx.fillStyle = 'rgba(255, 215, 0, 0.06)';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(mouseX, mouseY, hitRadius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  } else if (tool === TOOLS.ELECTRIC) {
    // Lightning cursor indicator
    ctx.fillStyle = 'rgba(100, 180, 255, 0.9)';
    ctx.font = '22px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('⚡', mouseX, mouseY);
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';
  } else if (tool === TOOLS.COIL) {
    // Coil cursor
    ctx.fillStyle = 'rgba(139, 195, 74, 0.9)';
    ctx.font = '22px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('🪴', mouseX, mouseY);
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';
  }

  ctx.restore();
}

// ======================== INPUT HANDLING ========================
function getCanvasPos(e) {
  const rect = $canvas.getBoundingClientRect();
  const scaleX = $canvas.width / rect.width;
  const scaleY = $canvas.height / rect.height;
  return {
    x: (e.clientX - rect.left) * scaleX,
    y: (e.clientY - rect.top) * scaleY,
  };
}

$canvas.addEventListener('mousemove', (e) => {
  const pos = getCanvasPos(e);
  mouseX = pos.x;
  mouseY = pos.y;
  mouseOnCanvas = true;

  // Coil placement indicator follows mouse
  if (Tools.isPlacingCoil) {
    $coilIndicator.style.left = e.clientX + 'px';
    $coilIndicator.style.top = e.clientY + 'px';
  }
});

$canvas.addEventListener('mouseleave', () => {
  mouseOnCanvas = false;
  // Keep electric cursor visible if still dragging
  if (!Tools.isDragging) {
    $electricCursor.classList.add('hidden');
  }
  $coilIndicator.classList.add('hidden');
});

$canvas.addEventListener('mouseenter', () => {
  mouseOnCanvas = true;
});

$canvas.addEventListener('mousedown', (e) => {
  if (currentState !== STATE.PLAYING) return;
  if (e.button !== 0) return; // left click only

  Audio.resume();

  // Coil placement mode
  if (Tools.isPlacingCoil) {
    const pos = getCanvasPos(e);
    const placed = Tools.placeCoil(pos.x, pos.y);
    if (placed) {
      $coilIndicator.classList.add('hidden');
      updateHUD();
      updateToolBar();
    }
    return;
  }

  // Check if clicking on an existing coil to recycle
  const pos = getCanvasPos(e);
  const coilIndex = Tools.findCoilAt(pos.x, pos.y);
  if (coilIndex >= 0 && Tools.currentTool === TOOLS.COIL) {
    Tools.recycleCoil(coilIndex);
    updateHUD();
    return;
  }

  // Electric swatter: start drag
  if (Tools.currentTool === TOOLS.ELECTRIC) {
    Tools.startDrag(pos.x, pos.y);
    if (Tools.isDragging) {
      $electricCursor.classList.remove('hidden');
      $electricCursor.querySelector('img').src = 'assets/电蚊拍（常态）.png';
      $electricCursor.style.left = e.clientX + 'px';
      $electricCursor.style.top = e.clientY + 'px';
      updateHUD();
    }
    return;
  }

  // Hand / Flyswatter: click attack
  const clickInfo = Tools.canClick();
  if (!clickInfo) {
    // Blocked by cooldown, depletion, or insufficient stamina
    return;
  }

  // Check hit against mosquitoes FIRST (before deducting stamina)
  let hitAny = false;
  for (const m of mosquitoes) {
    if (!m.alive) continue;
    if (m.hitTest(pos.x, pos.y, clickInfo.hitRadiusMultiplier)) {
      hitAny = true;
      break;
    }
  }

  if (hitAny) {
    // Hit! Deduct tool stamina and kill all mosquitoes in range
    if (!Player.spendStamina(clickInfo.cost)) return;
    for (const m of mosquitoes) {
      if (!m.alive) continue;
      if (m.hitTest(pos.x, pos.y, clickInfo.hitRadiusMultiplier)) {
        killMosquito(m, clickInfo.type);
      }
    }

    if (clickInfo.type === 'swatter') {
      Audio.playSlap();
      Tools.triggerShake();
      swatterSwish = { x: pos.x, y: pos.y, time: 0.3, maxTime: 0.3 };
    } else {
      Audio.playSlap();
    }
    updateHUD();
    updateToolBar();
  } else {
    // Miss: deduct only miss penalty (1 stamina)
    Player.missClick();
    Audio.playBeep();
    updateHUD();
  }
});

document.addEventListener('mouseup', (e) => {
  if (currentState !== STATE.PLAYING) return;

  // End electric drag
  if (Tools.isDragging) {
    const path = Tools.endDrag();
    $electricCursor.classList.add('hidden');

    if (path.length >= 2) {
      // Check collision with mosquitoes along the drag path
      let hitAny = false;
      for (const m of mosquitoes) {
        if (!m.alive) continue;

        // Check each segment of the path
        for (let i = 1; i < path.length; i++) {
          if (m.lineHitTest(path[i - 1].x, path[i - 1].y, path[i].x, path[i].y, 2.5)) {
            killMosquito(m, 'zap', path[i].x, path[i].y);
            hitAny = true;
            break;
          }
        }
      }

      if (hitAny) {
        Audio.playZap();
        updateHUD();
        updateToolBar();
      }
    }
  }
});

// Document-level mousemove for electric drag tracking (works outside canvas)
document.addEventListener('mousemove', (e) => {
  if (currentState !== STATE.PLAYING) return;
  if (!Tools.isDragging) return;

  // Convert client pos to canvas pos for path tracking
  const rect = $canvas.getBoundingClientRect();
  const scaleX = $canvas.width / rect.width;
  const scaleY = $canvas.height / rect.height;
  const cx = (e.clientX - rect.left) * scaleX;
  const cy = (e.clientY - rect.top) * scaleY;

  // Check collision with mosquitoes on the fly (kill on contact)
  const prevLen = Tools.dragPath.length;
  let hitAny = false;
  if (prevLen > 0) {
    const lastPt = Tools.dragPath[prevLen - 1];
    for (const m of mosquitoes) {
      if (!m.alive) continue;
      if (m.lineHitTest(lastPt.x, lastPt.y, cx, cy, 2.5)) {
        killMosquito(m, 'zap', cx, cy);
        hitAny = true;
      }
    }
    if (hitAny) {
      Audio.playZap();
      updateHUD();
      updateToolBar();
    }
  }

  Tools.updateDrag(cx, cy);

  // Update electric cursor DOM element
  $electricCursor.style.left = e.clientX + 'px';
  $electricCursor.style.top = e.clientY + 'px';

  // Add to trail for VFX
  if (electricTrail.length === 0 || electricTrail[electricTrail.length - 1].life < 0.05) {
    if (electricTrail.length > 0) {
      const last = electricTrail[electricTrail.length - 1];
      electricTrail.push({
        x1: last.x2, y1: last.y2,
        x2: cx, y2: cy,
        life: 0.2,
      });
    } else {
      // First trail segment
      electricTrail.push({
        x1: cx - 1, y1: cy,
        x2: cx, y2: cy,
        life: 0.2,
      });
    }
  }
});

// Prevent context menu on game canvas
$canvas.addEventListener('contextmenu', e => e.preventDefault());

// ======================== KILL A MOSQUITO ========================
function killMosquito(m, method, fx, fy) {
  const x = fx || m.x;
  const y = fy || m.y;

  if (method === 'zap') {
    Particles.emitZap(x, y);
    // Flash VFX
    Tools.lastHitVFX = { type: 'zap', x, y, time: 0.3 };
  } else {
    Particles.emitBlood(x, y);
    Tools.lastHitVFX = { type: 'slap', x, y, time: 0.3 };
  }

  // Increment kill count
  Player.addKill();

  // Respawn mosquito at edge
  m.spawn();
}

// ======================== KEYBOARD INPUT ========================
document.addEventListener('keydown', (e) => {
  // Global: ESC handling
  if (e.key === 'Escape') {
    if (currentState === STATE.PLAYING) {
      pauseGame();
    } else if (currentState === STATE.PAUSED) {
      // Only resume via ESC if on the basic pause screen (has resume button)
      if (document.getElementById('btn-resume')) {
        resumeGame();
      }
    }
    return;
  }

  // Game-only keys
  if (currentState !== STATE.PLAYING) return;

  // Number keys 1-4 for tool switching
  if (e.key >= '1' && e.key <= '4') {
    const toolIndex = parseInt(e.key) - 1;
    if (!Player.unlockedTools[toolIndex]) return;

    selectedToolIndex = toolIndex;

    if (toolIndex === TOOLS.COIL) {
      // Toggle coil placement mode
      if (Tools.isPlacingCoil) {
        Tools.isPlacingCoil = false;
        $coilIndicator.classList.add('hidden');
      } else {
        if (Tools.startCoilPlacement()) {
          Tools.currentTool = TOOLS.COIL;
          $coilIndicator.classList.remove('hidden');
        }
      }
    } else {
      Tools.isPlacingCoil = false;
      $coilIndicator.classList.add('hidden');
      Tools.switchTool(toolIndex);
    }
    updateToolBar();
  }
});

// ======================== GAME PAUSE / RESUME ========================
function pauseGame() {
  setState(STATE.PAUSED);
  try { Audio.setAmbientFiltered(true); } catch (e) { /* audio might not be ready */ }
  showPauseModal();
}

function resumeGame() {
  setState(STATE.PLAYING);
  try { Audio.setAmbientFiltered(false); } catch (e) {}
  hideModal();
  lastTime = performance.now();
}

function showPauseModal() {
  // Make sure game screen cursor is visible for modal interaction
  if ($game) $game.style.cursor = 'default';
  if (!$modalOverlay || !$modalBox) return;

  $modalOverlay.classList.remove('hidden');
  $modalBox.innerHTML = `
    <h2>⏸️ 游戏暂停</h2>
    <div class="modal-kills">🏆 ${Player.kills}</div>
    <p>击杀数</p>
    <div class="modal-buttons">
      <button class="btn btn-primary" id="btn-resume">继续游戏</button>
      <button class="btn btn-secondary" id="btn-end-no-save">直接结束</button>
      <button class="btn btn-primary" id="btn-end-save">计入排行榜</button>
    </div>
  `;

  document.getElementById('btn-resume').addEventListener('click', resumeGame);
  document.getElementById('btn-end-no-save').addEventListener('click', endGameNoSave);
  document.getElementById('btn-end-save').addEventListener('click', endGameWithSave);
}

function hideModal() {
  $modalOverlay.classList.add('hidden');
  // Restore game cursor hidden
  $game.style.cursor = '';
}

// ======================== END GAME FLOW ========================
function endGameNoSave() {
  try {
    hideModal();
    cleanupGame();
  } catch (e) {
    console.warn('endGameNoSave cleanup error:', e);
  }
  // Force transition to menu — always execute even if cleanup throws
  $modalOverlay.classList.add('hidden');
  $duplicateModal.classList.add('hidden');
  $game.classList.add('hidden');
  $game.style.cursor = '';
  $menu.classList.remove('hidden');
  $menu.style.display = '';
  $leaderboard.classList.add('hidden');
  setState(STATE.MENU);
  spawnMenuMosquitoes();
}

function endGameWithSave() {
  // Show name input in modal
  $modalBox.innerHTML = `
    <h2>🏆 保存记录</h2>
    <div class="modal-kills">${Player.kills}</div>
    <p>击杀数</p>
    <input type="text" id="input-nickname" placeholder="请输入你的昵称..." maxlength="20" autofocus>
    <p class="modal-error hidden" id="modal-error"></p>
    <div class="modal-buttons">
      <button class="btn btn-primary" id="btn-save-confirm">确认保存</button>
      <button class="btn btn-ghost" id="btn-save-cancel">取消</button>
    </div>
  `;

  const input = document.getElementById('input-nickname');
  const errorEl = document.getElementById('modal-error');

  document.getElementById('btn-save-cancel').addEventListener('click', () => {
    try { hideModal(); cleanupGame(); } catch (e) {}
    // Force transition to menu
    $modalOverlay.classList.add('hidden');
    $duplicateModal.classList.add('hidden');
    $game.classList.add('hidden');
    $game.style.cursor = '';
    $menu.classList.remove('hidden');
    $menu.style.display = '';
    $leaderboard.classList.add('hidden');
    setState(STATE.MENU);
    spawnMenuMosquitoes();
  });

  document.getElementById('btn-save-confirm').addEventListener('click', () => {
    const name = input.value.trim();
    if (!name) {
      errorEl.textContent = '昵称不能为空';
      errorEl.classList.remove('hidden');
      return;
    }

    // Check for duplicate
    if (LB.nameExists(name)) {
      const existing = LB.findByName(name);
      if (Player.kills > existing.kills) {
        // Show duplicate modal with overwrite option
        showDuplicateModal(name, existing);
      } else {
        // Can't overwrite — kills not higher
        showDuplicateModal(name, existing);
      }
    } else {
      // Safe to save
      const result = LB.addEntry(name, Player.kills, false);
      if (result.success) {
        try {
          hideModal();
          cleanupGame();
          showLeaderboard();
        } catch (err) {
          console.warn('Save→leaderboard failed:', err);
          // Fallback: force back to menu
          $modalOverlay.classList.add('hidden');
          $duplicateModal.classList.add('hidden');
          $game.classList.add('hidden');
          $game.style.cursor = '';
          showScreen('menu-screen');
          setState(STATE.MENU);
          spawnMenuMosquitoes();
        }
      } else {
        errorEl.textContent = result.error || '保存失败';
        errorEl.classList.remove('hidden');
      }
    }
  });

  // Enter key to submit
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      document.getElementById('btn-save-confirm').click();
    }
  });

  // Focus input
  setTimeout(() => input.focus(), 100);
}

function showDuplicateModal(name, existingEntry) {
  $duplicateModal.classList.remove('hidden');
  const msg = document.getElementById('duplicate-message');
  const overwriteBtn = document.getElementById('dup-overwrite');
  const renameBtn = document.getElementById('dup-rename');
  const cancelBtn = document.getElementById('dup-cancel');

  if (Player.kills > existingEntry.kills) {
    msg.textContent = `"${name}" 已存在（击杀: ${existingEntry.kills}）。新击杀数更高，可以覆盖旧记录。`;
    overwriteBtn.disabled = false;
  } else {
    msg.textContent = `"${name}" 已存在（击杀: ${existingEntry.kills}）。新击杀数未超过旧记录，无法覆盖。`;
    overwriteBtn.disabled = true;
  }

  function hideDupModal() {
    $duplicateModal.classList.add('hidden');
  }

  // Use onclick to auto-replace previous handlers
  overwriteBtn.onclick = () => {
    const result = LB.addEntry(name, Player.kills, true);
    if (result.success) {
      try {
        hideDupModal();
        hideModal();
        cleanupGame();
        showLeaderboard();
      } catch (err) {
        console.warn('Overwrite→leaderboard failed:', err);
        $modalOverlay.classList.add('hidden');
        $duplicateModal.classList.add('hidden');
        $game.classList.add('hidden');
        $game.style.cursor = '';
        showScreen('menu-screen');
        setState(STATE.MENU);
        spawnMenuMosquitoes();
      }
    }
  };

  renameBtn.onclick = () => {
    hideDupModal();
    endGameWithSave();
  };

  cancelBtn.onclick = () => {
    hideDupModal();
    showPauseModal();
  };
}

function cleanupGame() {
  if (animFrameId) {
    cancelAnimationFrame(animFrameId);
    animFrameId = null;
  }
  Audio.stopAmbientBuzz();
  Audio.setAmbientFiltered(false);
  hideModal();
  $duplicateModal.classList.add('hidden');
  $electricCursor.classList.add('hidden');
  $coilIndicator.classList.add('hidden');
  // Force-hide game screen and reset cursor
  $game.classList.add('hidden');
  $game.style.cursor = '';
}

// ======================== HUD UPDATES ========================
function updateHUD() {
  // Stamina bar
  const pct = Player.getStaminaPercent();
  $staminaFill.style.width = `${pct * 100}%`;
  $staminaFill.className = Player.getStaminaClass();
  $staminaText.textContent = `${Math.ceil(Player.stamina)}/${Player.maxStamina}`;

  // Kill count
  $killCount.textContent = Player.kills;

  // Coil count
  $coilPlaced.textContent = Tools.coils.length;
  $coilMax.textContent = Player.getMaxCoils();
}

function updateToolBar() {
  for (let i = 0; i < 4; i++) {
    const slot = $toolSlots[i];
    const isUnlocked = Player.unlockedTools[i];

    if (isUnlocked) {
      slot.classList.remove('locked');
      const lockEl = slot.querySelector('.tool-lock');
      if (lockEl) lockEl.style.display = 'none';
    } else {
      slot.classList.add('locked');
      const lockEl = slot.querySelector('.tool-lock');
      if (lockEl) lockEl.style.display = '';
    }

    // Selected state
    if (i === selectedToolIndex && isUnlocked && !Tools.isPlacingCoil) {
      slot.classList.add('selected');
    } else if (i === TOOLS.COIL && Tools.isPlacingCoil) {
      slot.classList.add('selected');
    } else {
      slot.classList.remove('selected');
    }
  }
}

function flashToolSlot(toolIndex) {
  const slot = $toolSlots[toolIndex];
  slot.style.transition = 'all 0.15s';
  slot.style.transform = 'scale(1.2)';
  slot.style.boxShadow = '0 0 20px rgba(255, 145, 0, 0.7)';
  setTimeout(() => {
    slot.style.transform = 'scale(1)';
    slot.style.boxShadow = '';
  }, 300);
}

function updateNewbieHint() {
  const next = Player.getNextUnlock();
  if (next && next.needed <= 3 && next.needed > 0) {
    $newbieHint.classList.remove('hidden');
    $hintKillsRemaining.textContent = next.needed;
    const bubble = $newbieHint.querySelector('.hint-bubble');
    if (bubble) {
      bubble.textContent = `💡 再击杀 ${next.needed} 只，解锁强力${next.name}！`;
    }
  } else {
    $newbieHint.classList.add('hidden');
  }
}

// ======================== TOOL BAR CLICKS ========================
$toolSlots.forEach((slot, index) => {
  slot.addEventListener('click', () => {
    if (currentState !== STATE.PLAYING) return;
    if (!Player.unlockedTools[index]) return;

    selectedToolIndex = index;

    if (index === TOOLS.COIL) {
      // Toggle placement mode
      if (Tools.isPlacingCoil) {
        Tools.isPlacingCoil = false;
        $coilIndicator.classList.add('hidden');
      } else {
        if (Tools.startCoilPlacement()) {
          Tools.currentTool = TOOLS.COIL;
          $coilIndicator.classList.remove('hidden');
        }
      }
    } else {
      Tools.isPlacingCoil = false;
      $coilIndicator.classList.add('hidden');
      Tools.switchTool(index);
    }
    updateToolBar();
  });
});

// ======================== PAUSE BUTTON ========================
document.getElementById('btn-pause').addEventListener('click', () => {
  if (currentState === STATE.PLAYING) {
    pauseGame();
  }
});

// ======================== LEADERBOARD ========================
function showLeaderboard() {
  showScreen('leaderboard-screen');
  setState(STATE.LEADERBOARD);
  renderLeaderboard();
  $searchInput.value = '';
  $searchInput.focus();
}

function renderLeaderboard(query = '') {
  const entries = LB.getEntries(query);

  if (entries.length === 0) {
    $leaderboardTbody.innerHTML = '';
    $leaderboardEmpty.classList.remove('hidden');
    $searchNoResult.classList.toggle('hidden', !query);
  } else {
    $leaderboardEmpty.classList.add('hidden');
    $searchNoResult.classList.toggle('hidden', true);

    $leaderboardTbody.innerHTML = entries.map(e => {
      let rankClass = '';
      if (e.rank === 1) rankClass = 'rank-1';
      else if (e.rank === 2) rankClass = 'rank-2';
      else if (e.rank === 3) rankClass = 'rank-3';

      let rankDisplay = e.rank;
      if (e.rank === 1) rankDisplay = '🥇';
      else if (e.rank === 2) rankDisplay = '🥈';
      else if (e.rank === 3) rankDisplay = '🥉';

      return `
        <tr>
          <td class="${rankClass}">${rankDisplay}</td>
          <td>${escapeHtml(e.name)}</td>
          <td><strong>${e.kills}</strong></td>
          <td>${e.date}</td>
        </tr>
      `;
    }).join('');
  }
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// Leaderboard search
$searchInput.addEventListener('input', () => {
  const query = $searchInput.value;
  renderLeaderboard(query);
});

// ======================== MENU BUTTONS ========================
document.getElementById('btn-start').addEventListener('click', () => {
  Audio.resume();
  Audio.init().then(() => {
    startGame();
  }).catch(() => {
    // Audio init may fail, still start game
    startGame();
  });
});

document.getElementById('btn-leaderboard').addEventListener('click', () => {
  Audio.resume();
  Audio.init().then(() => {
    showLeaderboard();
  });
});

document.getElementById('btn-back-from-leaderboard').addEventListener('click', () => {
  // Ensure game is fully cleaned up before going to menu
  if (animFrameId) {
    cancelAnimationFrame(animFrameId);
    animFrameId = null;
  }
  $modalOverlay.classList.add('hidden');
  $duplicateModal.classList.add('hidden');
  $game.classList.add('hidden');
  $game.style.cursor = '';
  showScreen('menu-screen');
  setState(STATE.MENU);
  spawnMenuMosquitoes();
});

// ======================== START GAME ========================
function startGame() {
  initGame();
  showScreen('game-screen');
  setState(STATE.PLAYING);

  // Size canvas
  resizeCanvas();
  window.addEventListener('resize', resizeCanvas);

  // Start game loop
  lastTime = performance.now();
  animFrameId = requestAnimationFrame(gameLoop);

  // Hide cursor elements
  $electricCursor.classList.add('hidden');
  $coilIndicator.classList.add('hidden');

  updateHUD();
  updateToolBar();
}

// ======================== CANVAS RESIZE ========================
function resizeCanvas() {
  const designW = 1920;
  const designH = 1080;

  const windowW = window.innerWidth;
  const windowH = window.innerHeight;

  // Fit canvas to window while maintaining aspect ratio
  const scaleX = windowW / designW;
  const scaleY = windowH / designH;
  const scale = Math.min(scaleX, scaleY);

  $canvas.width = designW;
  $canvas.height = designH;
  $canvas.style.width = (designW * scale) + 'px';
  $canvas.style.height = (designH * scale) + 'px';

  // Center the canvas
  $canvas.style.position = 'absolute';
  $canvas.style.left = ((windowW - designW * scale) / 2) + 'px';
  $canvas.style.top = ((windowH - designH * scale) / 2) + 'px';

  // Update game area for mosquito boundary
  setGameArea(designW, designH);
}

window.addEventListener('resize', () => {
  if (currentState === STATE.PLAYING || currentState === STATE.PAUSED) {
    resizeCanvas();
  }
});

// ======================== MENU BACKGROUND MOSQUITOES ========================
function spawnMenuMosquitoes() {
  const container = document.getElementById('menu-mosquitoes');
  if (!container) return;
  container.innerHTML = '';
  for (let i = 0; i < 8; i++) {
    const el = document.createElement('div');
    el.className = 'bg-mosquito';
    el.textContent = '🦟';
    el.style.left = Math.random() * 90 + '%';
    el.style.top = Math.random() * 90 + '%';
    el.style.animationDelay = Math.random() * 8 + 's';
    el.style.animationDuration = (6 + Math.random() * 10) + 's';
    container.appendChild(el);
  }
}

// ======================== BOOTSTRAP ========================
async function bootstrap() {
  // Show loading
  $loading.classList.remove('hidden');
  $menu.classList.add('hidden');

  // Preload image assets
  const images = await preloadAssets();
  mosquitoImg = images.mosquito || null;
  flyswatterImg = images.flyswatter || null;
  electricNormalImg = images.electricNormal || null;
  electricZappingImg = images.electricZapping || null;
  coilImg = images.coil || null;

  // Audio is initialized on first user click (browser autoplay policy)
  // Loading complete
  setTimeout(() => {
    $loading.classList.add('hidden');
    $menu.classList.remove('hidden');
    setState(STATE.MENU);
    spawnMenuMosquitoes();
    resizeCanvas();
  }, 500);
}

// Start
bootstrap();

// Prevent accidental navigation
window.addEventListener('beforeunload', (e) => {
  if (currentState === STATE.PLAYING) {
    e.preventDefault();
    e.returnValue = '游戏进行中，确定要离开吗？';
    return e.returnValue;
  }
});
