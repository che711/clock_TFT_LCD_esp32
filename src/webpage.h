#pragma once

// Статичный HTML-дашборд.
// Данные получает через fetch("/api/stats") раз в секунду.
// Управление: /api/power?on=1|0 и /api/brightness?value=0..100

const char WEBPAGE[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 Clock</title>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'><text y='9' font-size='10'>⚡</text></svg>">
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: #080d18;
    color: #c9d1d9;
    font-family: 'Segoe UI', 'Courier New', monospace;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 20px;
    gap: 14px;
  }

  /* ── Часы ── */
  .clock-card {
    background: #0d1117;
    border: 1px solid #21262d;
    border-radius: 18px;
    padding: 28px 40px 22px;
    text-align: center;
    width: 100%;
    max-width: 520px;
    box-shadow: 0 8px 24px rgba(0,0,0,0.4);
    transition: border-color .3s;
  }
  .clock-card.error { border-color: #da3633; }

  .chip-label {
    font-size: .7em;
    color: #484f58;
    text-transform: uppercase;
    letter-spacing: .16em;
    margin-bottom: 14px;
  }

  .time {
    font-size: 4.6em;
    font-weight: 600;
    color: #e6edf3;
    letter-spacing: .08em;
    line-height: 1;
    font-variant-numeric: tabular-nums;
    transition: color .4s;
  }
  .time .secs {
    font-size: .55em;
    opacity: .45;
    vertical-align: middle;
  }
  .time.err { color: #f85149; }

  .date {
    font-size: 1em;
    color: #8b949e;
    margin-top: 10px;
  }

  /* ── Управление ── */
  .ctrl-card {
    background: #0d1117;
    border: 1px solid #21262d;
    border-radius: 14px;
    padding: 16px 20px;
    width: 100%;
    max-width: 520px;
  }
  .ctrl-title {
    font-size: .68em;
    color: #484f58;
    text-transform: uppercase;
    letter-spacing: .12em;
    margin-bottom: 14px;
    border-bottom: 1px solid #161b22;
    padding-bottom: 7px;
  }
  .ctrl-row {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 12px;
  }
  .ctrl-row:last-child { margin: 0; }
  .ctrl-lbl { color: #8b949e; font-size: .82em; min-width: 80px; }

  .btn {
    padding: 7px 20px;
    border: 2px solid transparent;
    border-radius: 8px;
    cursor: pointer;
    font-weight: bold;
    font-size: .82em;
    transition: all .2s;
    opacity: .45;
  }
  .btn:hover     { transform: translateY(-1px); opacity: .8; }
  .btn.active    { opacity: 1; border-color: rgba(255,255,255,.2); }
  .btn-on        { background: #238636; color: #fff; }
  .btn-off       { background: #da3633; color: #fff; }

  .pwr-state { font-size: .82em; margin-left: 2px; }

  input[type=range] {
    flex: 1;
    accent-color: #58a6ff;
    cursor: pointer;
  }
  .bright-val { color: #e6edf3; font-size: .85em; min-width: 42px; text-align: right; }

  /* ── Карточки ── */
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    width: 100%;
    max-width: 520px;
  }
  @media (max-width: 480px) {
    .grid { grid-template-columns: 1fr; }
    .time { font-size: 4em; }
  }

  .card {
    background: #0d1117;
    border: 1px solid #21262d;
    border-radius: 14px;
    padding: 14px 16px;
  }
  .card-title {
    font-size: .68em;
    color: #484f58;
    text-transform: uppercase;
    letter-spacing: .12em;
    margin-bottom: 10px;
    border-bottom: 1px solid #161b22;
    padding-bottom: 6px;
  }
  .row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 5px 0;
    font-size: .82em;
    border-bottom: 1px solid #0d0d12;
  }
  .row:last-child { border: none; }
  .lbl { color: #8b949e; }
  .val { color: #e6edf3; text-align: right; }

  /* ── RAM bar ── */
  .bar-wrap { flex: 1; background: #21262d; height: 7px; border-radius: 4px; margin: 8px 0 4px; }
  .bar-fill { height: 100%; border-radius: 4px; background: #1f6feb; transition: width .6s; }

  /* ── Toast ── */
  .toast {
    position: fixed;
    bottom: 28px;
    right: 28px;
    padding: 10px 18px;
    border-radius: 8px;
    font-size: .82em;
    font-weight: bold;
    color: #fff;
    opacity: 0;
    transition: opacity .3s;
    pointer-events: none;
    z-index: 999;
  }

  .footer { font-size: .65em; color: #30363d; margin-top: 4px; }

  /* ── Цвета ── */
  .green  { color: #3fb950; }
  .yellow { color: #d29922; }
  .red    { color: #f85149; }
  .blue   { color: #58a6ff; }
  .cyan   { color: #39d0d8; }
</style>
</head>
<body>

<!-- Toast -->
<div class="toast" id="toast"></div>

<!-- Часы -->
<div class="clock-card" id="clock-card">
  <div class="chip-label" id="chip-label">ESP32-S3 · connecting…</div>
  <div class="time" id="time">--:--<span class="secs">:--</span></div>
  <div class="date" id="date">---</div>
</div>

<!-- Управление -->
<div class="ctrl-card">
  <div class="ctrl-title">Display Control</div>

  <div class="ctrl-row">
    <span class="ctrl-lbl">Power</span>
    <button class="btn btn-on"  id="btn-on"  onclick="setPower(true)">⏻ ON</button>
    <button class="btn btn-off" id="btn-off" onclick="setPower(false)">⏻ OFF</button>
    <span class="pwr-state" id="pwr-state"></span>
  </div>

  <div class="ctrl-row">
    <span class="ctrl-lbl">Brightness</span>
    <input type="range" id="bright-slider" min="0" max="100" value="78"
           oninput="document.getElementById('bright-val').textContent=this.value+'%'"
           onchange="setBrightness(this.value)">
    <span class="bright-val" id="bright-val">78%</span>
  </div>
</div>

<!-- Карточки -->
<div class="grid">

  <!-- WiFi -->
  <div class="card">
    <div class="card-title">WiFi</div>
    <div class="row"><span class="lbl">SSID</span>   <span class="val blue"  id="ssid">…</span></div>
    <div class="row"><span class="lbl">IP</span>     <span class="val green" id="ip">…</span></div>
    <div class="row"><span class="lbl">Signal</span> <span class="val"       id="rssi">…</span></div>
  </div>

  <!-- System -->
  <div class="card">
    <div class="card-title">System</div>
    <div class="row"><span class="lbl">Uptime</span> <span class="val cyan"   id="uptime">…</span></div>
    <div class="row"><span class="lbl">Temp</span>   <span class="val"        id="temp">…</span></div>
    <div class="row">
      <span class="lbl">CPU</span>
      <span class="val" id="cpu-val" style="min-width:36px;text-align:right">…</span>
    </div>
    <div class="bar-wrap" style="margin:2px 0 6px">
      <div class="bar-fill" id="cpu-bar" style="width:0%;background:#39d0d8"></div>
    </div>
    <div class="row"><span class="lbl">Reset</span>  <span class="val"        id="reset">…</span></div>
  </div>

  <!-- RAM -->
  <div class="card" style="grid-column:1/-1">
    <div class="card-title">Memory <span id="ram-pct-lbl" style="font-weight:normal;color:#58a6ff"></span></div>
    <div class="bar-wrap"><div class="bar-fill" id="ram-bar" style="width:0%"></div></div>
    <div class="row"><span class="lbl">Free</span>     <span class="val green"  id="heap-free">…</span></div>
    <div class="row"><span class="lbl">Used</span>     <span class="val"        id="heap-used">…</span></div>
    <div class="row"><span class="lbl">Total</span>    <span class="val"        id="heap-total">…</span></div>
    <div class="row"><span class="lbl">Min ever</span> <span class="val yellow" id="heap-min">…</span></div>
  </div>

</div>

<div class="footer" id="footer">–</div>

<script>
// ── Утилиты ──────────────────────────────────────────────────────
const kb = n => n >= 1048576
  ? (n/1048576).toFixed(1) + ' MB'
  : (n/1024).toFixed(0) + ' KB';

function toast(msg, ok = true) {
  const el = document.getElementById('toast');
  el.textContent  = msg;
  el.style.background = ok ? '#238636' : '#da3633';
  el.style.opacity = '1';
  clearTimeout(el._t);
  el._t = setTimeout(() => el.style.opacity = '0', 2000);
}

// ── RSSI: цвет + полоски ─────────────────────────────────────────
function rssiHtml(r) {
  const q = r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : 1;
  const c = q >= 3 ? '#3fb950' : q === 2 ? '#d29922' : '#f85149';
  const bars = [1,2,3,4].map(i =>
    `<span style="opacity:${i<=q?1:.18}">▂</span>`
  ).join('');
  return `<span style="color:${c};font-size:.95em">${bars} ${r} dBm</span>`;
}

// ── Температура с цветом ─────────────────────────────────────────
function tempHtml(t) {
  const c = t < 50 ? '#3fb950' : t < 70 ? '#d29922' : '#f85149';
  return `<span style="color:${c}">${t.toFixed(1)} °C</span>`;
}

// ── Слайдер: не затирать пока тянут ─────────────────────────────
let sliderActive = false;
const slider = document.getElementById('bright-slider');
slider.addEventListener('pointerdown', () => sliderActive = true);
document.addEventListener('pointerup',  () => sliderActive = false);

// ── API вызовы ───────────────────────────────────────────────────
async function setPower(on) {
  try {
    await fetch('/api/power?on=' + (on ? '1' : '0'));
    toast(on ? '⏻ Display ON' : '⏻ Display OFF');
  } catch { toast('Connection error', false); }
}

async function setBrightness(val) {
  try {
    await fetch('/api/brightness?value=' + val);
    toast('☀ Brightness ' + val + '%');
  } catch { toast('Connection error', false); }
}

// ── Обновление секунд отдельно (без мерцания времени) ────────────
function setTime(t) {
  const parts = t.split(':');               // ["HH", "MM", "SS"]
  if (parts.length !== 3) return;
  const el = document.getElementById('time');
  el.innerHTML = parts[0] + ':' + parts[1] +
    '<span class="secs">:' + parts[2] + '</span>';
}

// ── Главный poll ─────────────────────────────────────────────────
async function refresh() {
  let d;
  try {
    d = await (await fetch('/api/stats')).json();
    // Сбросить индикацию ошибки
    document.getElementById('clock-card').classList.remove('error');
    document.getElementById('time').classList.remove('err');
  } catch {
    document.getElementById('clock-card').classList.add('error');
    document.getElementById('time').classList.add('err');
    document.getElementById('footer').textContent =
      '⚠ No connection · ' + new Date().toLocaleTimeString();
    return;
  }

  // Часы
  document.getElementById('chip-label').textContent =
    `${d.chip} · rev ${d.chip_rev} · ${d.cpu_mhz} MHz`;
  setTime(d.time);
  document.getElementById('date').textContent = d.date || '—';

  // WiFi
  document.getElementById('ssid').textContent = d.ssid;
  document.getElementById('ip').textContent   = d.ip;
  document.getElementById('rssi').innerHTML   = rssiHtml(d.rssi);

  // System
  document.getElementById('uptime').textContent = d.uptime;
  document.getElementById('temp').innerHTML      = tempHtml(d.temp);

  const cpuC = d.cpu_load < 50 ? '#39d0d8' : d.cpu_load < 80 ? '#d29922' : '#f85149';
  document.getElementById('cpu-val').textContent   = d.cpu_load + '%';
  document.getElementById('cpu-val').style.color   = cpuC;
  document.getElementById('cpu-bar').style.width   = d.cpu_load + '%';
  document.getElementById('cpu-bar').style.background = cpuC;

  document.getElementById('reset').textContent   = d.reset_reason;

  // RAM
  const used = d.heap_total - d.heap_free;
  const pct  = Math.round(used / d.heap_total * 100);
  document.getElementById('ram-bar').style.width  = pct + '%';
  document.getElementById('ram-pct-lbl').textContent = '— ' + pct + '% used';
  document.getElementById('heap-free').textContent   = kb(d.heap_free);
  document.getElementById('heap-used').textContent   = kb(used);
  document.getElementById('heap-total').textContent  = kb(d.heap_total);
  document.getElementById('heap-min').textContent    = kb(d.heap_min_free);

  // Кнопки Power — отражают реальное состояние ESP32
  const on = d.display_on;
  document.getElementById('btn-on').classList.toggle('active',  on);
  document.getElementById('btn-off').classList.toggle('active', !on);
  document.getElementById('pwr-state').textContent  = on ? '● ON' : '○ OFF';
  document.getElementById('pwr-state').style.color  = on ? '#3fb950' : '#f85149';

  // Слайдер — только если не тянут
  if (!sliderActive) {
    document.getElementById('bright-slider').value  = d.brightness;
    document.getElementById('bright-val').textContent = d.brightness + '%';
  }

  document.getElementById('footer').textContent =
    'Last update: ' + new Date().toLocaleTimeString();
}

refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)html";
