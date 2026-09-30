(function () {
  const el = (id) => document.getElementById(id);

  const wsStateEl   = el('wsState');
  const clockEl     = el('clock');
  const tempEl      = el('temp');
  const tempNoteEl  = el('tempNote');
  const swatchEl    = el('colorSwatch');
  const healthWordEl= el('healthWord');
  const healthDetailEl = el('healthDetail');
  const healthDotEl = el('healthDot');
  const luxEl       = el('lux');
  const ledStateEl  = el('ledState');
  const lightProgressEl = el('lightProgress');
  const sunHoursEl  = el('sunHours');
  const ledHoursEl  = el('ledHours');
  const targetHoursEl = el('targetHours');
  const coolStateEl = el('coolState');
  const alertbar    = el('alertbar');

  let ws;
  let wasHealthy = true;

  // ------------------------------------------------------------ Chart.js --
  // Wrapped defensively: if the Chart.js CDN ever fails to load (wrong
  // version pin, no internet, CDN outage), the rest of the dashboard --
  // most importantly the WebSocket connection below -- must keep working.
  // A single crashed line here should never be able to take down the whole
  // page again.
  let chart = null;
  try {
    const ctx = el('tempChart').getContext('2d');
    chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: 'Water temp (°C)',
          data: [],
          borderColor: '#3FD6C0',
          backgroundColor: 'rgba(63,214,192,0.12)',
          borderWidth: 2,
          pointRadius: 0,
          tension: 0.3,
          fill: true,
        }]
      },
      options: {
        responsive: true,
        animation: false,
        scales: {
          x: { ticks: { color: '#86998F', maxTicksLimit: 8 }, grid: { color: 'rgba(255,255,255,0.05)' } },
          y: { ticks: { color: '#86998F' }, grid: { color: 'rgba(255,255,255,0.05)' } }
        },
        plugins: { legend: { display: false } }
      }
    });
  } catch (e) {
    console.warn('Chart.js failed to initialize -- chart will be unavailable, rest of the dashboard continues.', e);
  }

  function fmtTime(epochSec) {
    if (!epochSec) return '--';
    const d = new Date(epochSec * 1000);
    return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }

  async function loadHistory() {
    if (!chart) return;
    try {
      const res = await fetch('/api/history');
      const points = await res.json();
      chart.data.labels = points.map(p => fmtTime(p.t));
      chart.data.datasets[0].data = points.map(p => p.temp);
      chart.update();
    } catch (e) {
      console.warn('history load failed', e);
    }
  }

  function pushChartPoint(state) {
    if (!chart) return;
    // Keep the live chart in sync between the 30s history samples too,
    // so the line feels alive without waiting for the next history push.
    const labels = chart.data.labels;
    const data = chart.data.datasets[0].data;
    const label = fmtTime(state.nowEpoch);
    if (labels[labels.length - 1] !== label) {
      labels.push(label);
      data.push(state.waterTemp);
      if (labels.length > 200) { labels.shift(); data.shift(); }
      chart.update('none');
    }
  }

  // --------------------------------------------------------- Notifications --
  function requestNotificationPermission() {
    if ('Notification' in window && Notification.permission === 'default') {
      Notification.requestPermission();
    }
  }

  function notify(title, body) {
    showAlertBar(body);
    if ('Notification' in window && Notification.permission === 'granted') {
      new Notification(title, { body });
    }
  }

  function showAlertBar(msg) {
    alertbar.textContent = msg;
    alertbar.hidden = false;
  }

  // ------------------------------------------------------------- Render --
  function render(state) {
    // temperature
    if (state.waterTemp !== null && state.waterTemp !== undefined) {
      tempEl.textContent = state.waterTemp.toFixed(1);
      tempNoteEl.textContent = state.coolingOn ? 'cooling active' : 'within range';
    } else {
      tempEl.textContent = '--';
      tempNoteEl.textContent = 'sensor not reporting';
    }

    // algae health
    const healthy = state.algaeHealthy;
    healthWordEl.textContent = healthy ? 'Healthy' : 'Pale — needs attention';
    healthWordEl.className = 'status-word ' + (healthy ? 'fresh' : 'pale');
    healthDetailEl.textContent =
      'green ratio ' + state.greenRatio.toFixed(2) + ' · saturation ' + state.saturation.toFixed(2);
    healthDotEl.className = 'brand-mark' + (healthy ? '' : ' pale');
    swatchEl.style.background = healthy
      ? 'linear-gradient(135deg, #4FAE63, #3FD6C0)'
      : 'linear-gradient(135deg, #BFAE5B, #8a7f45)';

    if (wasHealthy && !healthy) {
      notify('Algae needs attention', 'The algae color has turned pale — check the chamber.');
    }
    wasHealthy = healthy;

    // light
    luxEl.textContent = Math.round(state.lux) + ' lx';
    ledStateEl.textContent = state.ledOn ? 'on · ' + state.ledBrightnessPct + '%' : 'off';
    const pct = Math.min(100, ((state.sunlightHoursToday + state.ledHoursToday) / state.targetLightHours) * 100);
    lightProgressEl.style.width = pct + '%';
    sunHoursEl.textContent = state.sunlightHoursToday.toFixed(1);
    ledHoursEl.textContent = state.ledHoursToday.toFixed(1);
    targetHoursEl.textContent = state.targetLightHours;

    // cooling
    coolStateEl.textContent = state.coolingOn ? 'On' : 'Off';
    coolStateEl.className = 'status-word ' + (state.coolingOn ? 'pale' : 'fresh');

    // connection + clock
    clockEl.textContent = new Date(state.nowEpoch * 1000).toLocaleTimeString();

    pushChartPoint(state);
  }

  // -------------------------------------------------------------- socket --
  function connectWs() {
    const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    ws = new WebSocket(proto + location.host + '/ws');

    ws.onopen = () => {
      wsStateEl.textContent = 'live';
      wsStateEl.className = 'pill pill-on';
    };
    ws.onclose = () => {
      wsStateEl.textContent = 'reconnecting…';
      wsStateEl.className = 'pill pill-off';
      setTimeout(connectWs, 2000);
    };
    ws.onerror = () => ws.close();
    ws.onmessage = (evt) => {
      try {
        const state = JSON.parse(evt.data);
        render(state);
      } catch (e) {
        console.warn('bad ws payload', e);
      }
    };
  }

  requestNotificationPermission();
  loadHistory().then(connectWs);
})();
