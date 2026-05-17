#pragma once

// Статичная HTML-страница.
// Данные получает через fetch("/api/stats") раз в секунду.
// Никакой строковой конкатенации в C++ — только чистый HTML.

const char WEBPAGE[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3</title>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: #080d18;
    color: #c9d1d9;
    font-family: 'Courier New', monospace;
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
    border-radius: 16px;
    padding: 28px 40px 20px;
    text-align: center;
    width: 100%;
    max-width: 480px;
  }
  .chip-label {
    font-size: .68em;
    color: #484f58;
    text-transform: uppercase;
    letter-spacing: .15em;
    margin-bottom: 14px;
  }
  .time {
    font-size: 4.2em;
    color: #e6edf3;
    letter-spacing: .06em;
    line-height: 1;
    font-variant-numeric: tabular-nums;
  }
  .date {
    font-size: 1em;
    color: #8b949e;
    margin-top: 8px;
  }

  /* ── Карточки ── */
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
    width: 100%;
    max-width: 480px;
  }
  @media (max-width: 420px) { .grid { grid-template-columns: 1fr; } }

  .card {
    background: #0d1117;
    border: 1px solid #21262d;
    border-radius: 12px;
    padding: 14px 16px;
  }
  .card-title {
    font-size: .65em;
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
    padding: 4px 0;
    font-size: .78em;
    border-bottom: 1px solid #0d0d12;
  }
  .row:last-child { border: none; }
  .lbl { color: #484f58; }
  .val { color: #e6edf3; text-align: right; }

  /* ── Прогресс-бары ── */
  .bar-row {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 5px 0;
    font-size: .78em;
    border-bottom: 1px solid #0d0d12;
  }
  .bar-row:last-child { border: none; }
  .bar-lbl { color: #484f58; white-space: nowrap; min-width: 36px; }
  .bar-wrap { flex: 1; background: #161b22; border-radius: 3px; height: 5px; }
  .bar-fill { height: 5px; border-radius: 3px; transition: width .6s; }
  .bar-val  { color: #e6edf3; font-size: .9em; min-width: 42px; text-align: right; }
  .bar-ram  { background: #1f6feb; }
  .bar-temp { background: #d29922; }

  /* ── Цвета ── */
  .cyan   { color: #39d0d8; }
  .green  { color: #3fb950; }
  .yellow { color: #d29922; }
  .blue   { color: #58a6ff; }
  .red    { color: #f85149; }

  .footer {
    font-size: .6em;
    color: #21262d;
    margin-top: 4px;
  }

  /* ── Управление дисплеем ── */
  .ctrl-card {
    background: #0d1117;
    border: 1px solid #21262d;
    border-radius: 12px;
    padding: 14px 16px;
    width: 100%;
    max-width: 480px;
  }
  .ctrl-title {
    font-size: .65em;
    color: #484f58;
    text-transform: uppercase;
    letter-spacing: .12em;
    margin-bottom: 12px;
    border-bottom: 1px solid #161b22;
    padding-bottom: 6px;
  }
  .ctrl-row {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 10px;
  }
  .ctrl-row:last-child { margin: 0; }
  .ctrl-lbl { color: #484f58; font-size: .78em; min-width: 80px; }
  .btn {
    padding: 6px 20px;
    border: none; border-radius: 6px;
    cursor: pointer; font-family: inherit;
    font-size: .78em; font-weight: bold;
    transition: opacity .15s;
  }
  .btn:hover  { opacity: .75; }
  .btn-on     { background: #238636; color: #fff; }
  .btn-off    { background: #da3633; color: #fff; }
  .btn-active { outline: 2px solid #58a6ff; outline-offset: 2px; }
  .pwr-state  { font-size: .78em; }
  input[type=range] { flex: 1; accent-color: #58a6ff; cursor: pointer; }
  .bright-val { color: #e6edf3; font-size: .78em; min-width: 36px; text-align: right; }
</style>
</head>
<body>

<!-- Часы -->
<div class="clock-card">
  <div class="chip-label" id="chip-label">ESP32-S3 · connecting…</div>
  <div class="time" id="time">--:--:--</div>
  <div class="date" id="date">---</div>
</div>

<!-- Управление дисплеем -->
<div class="ctrl-card">
  <div class="ctrl-title">Display Control</div>

  <div class="ctrl-row">
    <span class="ctrl-lbl">Power</span>
    <button class="btn btn-on"  id="btn-on"  onclick="setPower(true)">⏻ ON</button>
    <button class="btn btn-off" id="btn-off" onclick="setPower(false)">⏻ OFF</button>
    <span class="pwr-state" id="pwr-state">…</span>
  </div>

  <div class="ctrl-row">
    <span class="ctrl-lbl">Brightness</span>
    <input type="range" id="bright-slider" min="0" max="100" value="78"
           oninput="document.getElementById('bright-val').textContent=this.value+'%'"
           onchange="setBrightness(this.value)">
    <span class="bright-val" id="bright-val">78%</span>
  </div>
</div>

<!-- Сетка карточек -->
<div class="grid">

  <!-- WiFi -->
  <div class="card">
    <div class="card-title">WiFi</div>
    <div class="row"><span class="lbl">SSID</span>   <span class="val blue"  id="ssid">…</span></div>
    <div class="row"><span class="lbl">IP</span>     <span class="val green" id="ip">…</span></div>
    <div class="row"><span class="lbl">Signal</span> <span class="val" id="rssi">…</span></div>
  </div>

  <!-- Система -->
  <div class="card">
    <div class="card-title">System</div>
    <div class="row"><span class="lbl">Uptime</span>  <span class="val cyan"   id="uptime">…</span></div>
    <div class="row"><span class="lbl">Temp</span>    <span class="val yellow" id="temp">…</span></div>
    <div class="row"><span class="lbl">Reset</span>   <span class="val"        id="reset">…</span></div>
  </div>

  <!-- RAM — на всю ширину -->
  <div class="card" style="grid-column: 1/-1">
    <div class="card-title">Memory (RAM)</div>
    <div class="bar-row">
      <span class="bar-lbl">Used</span>
      <div class="bar-wrap"><div class="bar-fill bar-ram" id="ram-bar" style="width:0%"></div></div>
      <span class="bar-val" id="ram-val">…</span>
    </div>
    <div class="row"><span class="lbl">Free</span>      <span class="val green" id="heap-free">…</span></div>
    <div class="row"><span class="lbl">Total</span>     <span class="val"       id="heap-total">…</span></div>
    <div class="row"><span class="lbl">Min ever</span>  <span class="val yellow" id="heap-min">…</span></div>
  </div>

</div>

<div class="footer" id="footer">–</div>

<script>
const kb = n => n >= 1048576
  ? (n/1048576).toFixed(1) + ' MB'
  : (n/1024).toFixed(1) + ' KB';
const rssiColor = r => r >= -60 ? 'green' : r >= -75 ? 'yellow' : 'red';

// Флаг: пользователь тянет слайдер прямо сейчас — не затирать значение из stats
let sliderActive = false;
document.getElementById('bright-slider').addEventListener('mousedown', () => sliderActive = true);
document.getElementById('bright-slider').addEventListener('touchstart', () => sliderActive = true);
document.addEventListener('mouseup',  () => sliderActive = false);
document.addEventListener('touchend', () => sliderActive = false);

async function setPower(on) {
  try {
    await fetch('/api/power?on=' + (on ? '1' : '0'));
  } catch(e) { console.error(e); }
}

async function setBrightness(val) {
  try {
    await fetch('/api/brightness?value=' + val);
  } catch(e) { console.error(e); }
}

async function refresh() {
  let d;
  try {
    d = await (await fetch('/api/stats')).json();
  } catch {
    document.getElementById('footer').textContent = 'Connection error · ' + new Date().toLocaleTimeString();
    return;
  }

  document.getElementById('chip-label').textContent =
    `${d.chip} · rev ${d.chip_rev} · ${d.cpu_mhz} MHz`;
  document.getElementById('time').textContent = d.time;
  document.getElementById('date').textContent = d.date;
  document.getElementById('ssid').textContent = d.ssid;
  document.getElementById('ip').textContent   = d.ip;

  const rssiEl = document.getElementById('rssi');
  rssiEl.textContent = `${d.rssi} dBm`;
  rssiEl.className   = `val ${rssiColor(d.rssi)}`;

  document.getElementById('uptime').textContent = d.uptime;
  const tempEl = document.getElementById('temp');
  tempEl.textContent = d.temp.toFixed(1) + ' °C';
  tempEl.className   = `val ${d.temp < 65 ? 'yellow' : 'red'}`;
  document.getElementById('reset').textContent = d.reset_reason;

  const pct = Math.round((1 - d.heap_free / d.heap_total) * 100);
  document.getElementById('ram-bar').style.width = pct + '%';
  document.getElementById('ram-val').textContent = pct + '% used';
  document.getElementById('heap-free').textContent  = kb(d.heap_free);
  document.getElementById('heap-total').textContent = kb(d.heap_total);
  document.getElementById('heap-min').textContent   = kb(d.heap_min_free);

  // ── Синхронизируем кнопки и слайдер со статусом ESP32 ──
  const on = d.display_on;
  document.getElementById('pwr-state').textContent = on ? '● ON' : '○ OFF';
  document.getElementById('pwr-state').style.color = on ? '#3fb950' : '#f85149';
  document.getElementById('btn-on').classList.toggle('btn-active',  on);
  document.getElementById('btn-off').classList.toggle('btn-active', !on);

  if (!sliderActive) {
    document.getElementById('bright-slider').value = d.brightness;
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
