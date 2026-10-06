const screen = document.querySelector('#screen');
const pageElement = document.querySelector('#page');

const state = {
  page: 'rpm',
  connected: true,
  rpm: 3280,
  speed: 86,
  temp: 92,
  voltage: 13.8,
  tires: { fl: 2.4, fr: 2.4, rl: 2.3, rr: 2.3 },
  pressureMin: 2.0,
  pressureMax: 3.2,
  theme: 'cyan',
  brightness: 82,
  racechrono: false,
  vehicle: 'ford-mondeo',
  bootPage: 'rpm',
  role: 'MASTER',
};

const pages = [
  ['rpm', '转速'], ['speed', '车速'], ['temp', '水温'], ['info', '信息'],
  ['needle', '增压'], ['tpms', '胎压'], ['multi', '多仪表'], ['settings', '设置'],
];

const format = (value, digits = 0) => Number(value).toFixed(digits);
const pageIndex = () => pages.findIndex(([id]) => id === state.page);
const pageName = () => pages[pageIndex()]?.[1] ?? '菜单';
const isPressureWarning = (value) => Number(value) > 0 &&
  (Number(value) < state.pressureMin || Number(value) > state.pressureMax);

function setPressureLimit(input) {
  const value = Number.parseFloat(input.value);
  if (!Number.isFinite(value)) return;

  if (input.id === 'pressureMin') {
    state.pressureMin = Math.min(value, state.pressureMax - 0.1);
    input.value = format(state.pressureMin, 1);
  } else {
    state.pressureMax = Math.max(value, state.pressureMin + 0.1);
    input.value = format(state.pressureMax, 1);
  }
}

function gauge(name, value, unit, percent, extras = '') {
  const amount = Math.min(100, Math.max(0, percent));
  return `<div class="gauge" style="--value:${amount}">
    <div class="gauge-content"><span class="gauge-name">${name}</span><strong class="gauge-value">${value}</strong><span class="gauge-unit">${unit}</span></div>${extras}
  </div>`;
}

function renderPage(direction = '') {
  const renderers = {
    rpm: () => gauge('发动机转速', format(state.rpm), 'rpm', state.rpm / 80, `<div class="mini-row"><span>水温 <b>${state.temp} C</b></span><span>电压 <b>${format(state.voltage, 1)} V</b></span></div>`),
    speed: () => gauge('车辆速度', format(state.speed), 'km/h', state.speed / 2.6, `<div class="mini-row"><span>档位 <b>${state.speed > 15 ? 'D' : 'N'}</b></span><span>转速 <b>${format(state.rpm)}</b></span></div>`),
    temp: () => gauge('冷却液温度', format(state.temp), 'C', (state.temp - 40) / .85, `<div class="mini-row"><span>机油 <b>98 C</b></span><span>进气 <b>34 C</b></span></div>`),
    needle: () => gauge('增压压力', '0.82', 'bar', 66, `<div class="mini-row"><span>数据源 <b>OBD PID</b></span></div>`),
    info: renderInfo,
    tpms: renderTpms,
    multi: renderMulti,
    settings: renderSettings,
    menu: renderMenu,
  };
  screen.dataset.page = state.page;
  pageElement.className = `page ${direction ? `enter-${direction}` : ''}`;
  pageElement.innerHTML = renderers[state.page]();
  updateStatus();
}

function renderInfo() {
  return `<div class="info-grid">
    <div class="info-tile"><span>转速</span><b>${format(state.rpm)}</b></div>
    <div class="info-tile"><span>车速</span><b>${format(state.speed)}</b></div>
    <div class="info-tile"><span>冷却液</span><b>${state.temp} C</b></div>
    <div class="info-tile"><span>电瓶电压</span><b>${format(state.voltage, 1)} V</b></div>
  </div>`;
}

function tire(label, position, value) {
  const warning = isPressureWarning(value);
  const side = position === 'fl' || position === 'rl' ? 'tire-left' : 'tire-right';
  const vertical = position === 'fl' || position === 'fr' ? 'tire-top' : 'tire-bottom';
  const content = vertical === 'tire-top'
    ? `<span>${label}</span><b>${format(value, 1)}</b><small>bar</small>`
    : `<small>bar</small><b>${format(value, 1)}</b><span>${label}</span>`;
  return `<div class="tire tire-${position} ${side} ${vertical} ${warning ? 'warning' : ''}"><div class="tire-content">${content}</div>${warning ? '<i class="warning-mark" role="img" aria-label="胎压异常">!</i>' : ''}</div>`;
}

function renderTpms() {
  return `<div class="tpms">
    <div class="tpms-header"><span>安全胎压</span><small>${format(state.pressureMin, 1)}-${format(state.pressureMax, 1)} BAR</small></div>
    ${tire('左前', 'fl', state.tires.fl)}
    ${tire('右前', 'fr', state.tires.fr)}
    ${tire('左后', 'rl', state.tires.rl)}
    ${tire('右后', 'rr', state.tires.rr)}
    <div class="voltage-card"><span>电瓶电压</span><b>${format(state.voltage, 1)}<em>V</em></b></div>
  </div>`;
}

function renderMulti() {
  const roleLabel = state.role === 'MASTER' ? '主机' : '从机 1';
  return `<div class="multigauge"><h2>${roleLabel} / 多仪表</h2>
    <div class="small-gauge"><span>转速</span><b>${format(state.rpm / 1000, 1)}k</b><small>rpm</small></div>
    <div class="small-gauge"><span>车速</span><b>${state.speed}</b><small>km/h</small></div>
    <div class="small-gauge"><span>电压</span><b>${format(state.voltage, 1)}</b><small>V</small></div>
    <button class="setting-toggle ${state.role === 'MASTER' ? 'active' : ''}" id="roleToggle">${state.role === 'MASTER' ? '设为从机' : '设为主机'}</button>
  </div>`;
}

function renderSettings() {
  return `<form class="settings" id="settingsForm">
    <h2>设置</h2>
    <label>开机页面<select id="bootPage">${pages.filter(([id]) => id !== 'settings').map(([id, name]) => `<option value="${id}" ${state.bootPage === id ? 'selected' : ''}>${name}</option>`).join('')}</select></label>
    <label>车辆<select id="vehicle"><option value="ford-mondeo" ${state.vehicle === 'ford-mondeo' ? 'selected' : ''}>福特 蒙迪欧</option><option value="subaru-brz" ${state.vehicle === 'subaru-brz' ? 'selected' : ''}>斯巴鲁 BRZ</option><option value="bmw-x1-f48" ${state.vehicle === 'bmw-x1-f48' ? 'selected' : ''}>宝马 X1 F48</option></select></label>
    <label>主题<select id="theme"><option value="cyan" ${state.theme === 'cyan' ? 'selected' : ''}>青色</option><option value="amber" ${state.theme === 'amber' ? 'selected' : ''}>琥珀色</option><option value="rose" ${state.theme === 'rose' ? 'selected' : ''}>玫瑰色</option></select></label>
    <div class="setting-row"><span>胎压范围</span><div class="pressure-range"><label>最低<input id="pressureMin" aria-label="最低胎压" type="number" min="0.1" max="${format(state.pressureMax - 0.1, 1)}" step="0.1" value="${format(state.pressureMin, 1)}"></label><label>最高<input id="pressureMax" aria-label="最高胎压" type="number" min="${format(state.pressureMin + 0.1, 1)}" max="4.0" step="0.1" value="${format(state.pressureMax, 1)}"></label></div></div>
    <label>亮度<input id="brightness" type="range" min="10" max="100" value="${state.brightness}"></label>
    <div class="setting-row"><span>RaceChrono</span><button type="button" class="setting-toggle ${state.racechrono ? 'active' : ''}" id="racechrono">${state.racechrono ? '开启' : '关闭'}</button></div>
  </form>`;
}

function renderMenu() {
  return `<div class="menu"><h2>选择页面</h2>${pages.map(([id, name]) => `<button data-page="${id}">${name}</button>`).join('')}</div>`;
}

function updateStatus() {
  document.querySelector('#connectionState').textContent = state.connected ? `OBD / ${pageName()}` : 'OBD 已断开';
  document.querySelector('#clock').textContent = new Intl.DateTimeFormat('zh-CN', { hour: '2-digit', minute: '2-digit', hour12: false }).format(new Date());
  screen.style.filter = `brightness(${state.brightness / 100})`;
}

function goTo(page, direction = '') {
  state.page = page;
  renderPage(direction);
}

function openPageMenu() {
  if (state.page === 'menu') return;
  goTo('menu');
}

function changePage(step) {
  if (state.page === 'menu') return;
  const current = pageIndex();
  const index = ((current < 0 ? 0 : current) + step + pages.length) % pages.length;
  goTo(pages[index][0], step > 0 ? 'left' : 'right');
}

function bindPageControls() {
  pageElement.addEventListener('click', (event) => {
    const target = event.target;
    if (target.matches('[data-page]')) goTo(target.dataset.page, 'left');
    if (target.id === 'roleToggle') { state.role = state.role === 'MASTER' ? 'SLAVE 1' : 'MASTER'; renderPage(); }
    if (target.id === 'racechrono') { state.racechrono = !state.racechrono; renderPage(); }
  });
  pageElement.addEventListener('change', (event) => {
    if (event.target.id === 'bootPage') state.bootPage = event.target.value;
    if (event.target.id === 'theme') { state.theme = event.target.value; screen.dataset.theme = state.theme; }
    if (event.target.id === 'vehicle') state.vehicle = event.target.value;
    if (event.target.id === 'brightness') { state.brightness = Number(event.target.value); updateStatus(); }
  });
  pageElement.addEventListener('input', (event) => {
    if (event.target.id === 'brightness') { state.brightness = Number(event.target.value); updateStatus(); }
    if (event.target.id === 'pressureMin' || event.target.id === 'pressureMax') setPressureLimit(event.target);
  });
}

function bindExternalControls() {
  const rangeControls = [
    ['rpmInput', 'rpm', 0, 'rpmOutput', ' rpm'], ['speedInput', 'speed', 0, 'speedOutput', ' km/h'],
    ['tempInput', 'temp', 0, 'tempOutput', ' C'], ['voltageInput', 'voltage', 1, 'voltageOutput', ' V'],
  ];
  rangeControls.forEach(([id, key, digits, output, suffix]) => {
    const input = document.querySelector(`#${id}`);
    input.addEventListener('input', () => { state[key] = Number(input.value); document.querySelector(`#${output}`).value = `${format(state[key], digits)}${suffix}`; renderPage(); });
  });
  ['fl', 'fr', 'rl', 'rr'].forEach((position) => {
    document.querySelector(`#${position}Input`).addEventListener('input', (event) => { state.tires[position] = Number(event.target.value); renderPage(); });
  });
  document.querySelector('#connectionToggle').addEventListener('click', () => {
    state.connected = !state.connected;
    const toggle = document.querySelector('#connectionToggle');
    toggle.classList.toggle('on', state.connected); toggle.setAttribute('aria-pressed', state.connected);
    document.querySelector('#connectionDetail').textContent = state.connected ? 'Vgate vLinker FD+ 模拟已连接' : '未连接，保留最后一组数据';
    renderPage();
  });
  document.querySelector('#resetSimulation').addEventListener('click', () => window.location.reload());
}

let gestureStart = null;
screen.addEventListener('pointerdown', (event) => {
  gestureStart = { x: event.clientX, y: event.clientY };
});
screen.addEventListener('pointerup', (event) => {
  if (!gestureStart) return;
  const deltaX = event.clientX - gestureStart.x;
  const deltaY = event.clientY - gestureStart.y;
  gestureStart = null;

  if (Math.abs(deltaX) > 35 && Math.abs(deltaX) > Math.abs(deltaY)) {
    changePage(deltaX < 0 ? 1 : -1);
  } else if (deltaY < -35 && Math.abs(deltaY) > Math.abs(deltaX)) {
    openPageMenu();
  }
});
screen.addEventListener('pointercancel', () => { gestureStart = null; });

bindPageControls();
bindExternalControls();
renderPage();
setInterval(updateStatus, 10000);
