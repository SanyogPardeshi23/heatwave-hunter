/*
 * app.js — draws what the C core (core.wasm) returns. No heatwave logic here:
 * every number, list, tree and search result comes from the C functions.
 */
'use strict';

const $ = (s, el = document) => el.querySelector(s);
const enc = new TextEncoder();
const dec = new TextDecoder();

let E = null;            // wasm exports
let META = null;         // years, cells, cities
let STATE = null;        // current day from C
let SEL = -1;            // selected cell index
let cellEls = [];
let highlight = new Set();
let showRule = false;
let playing = false, playTimer = null;
let logFilter = 'all';
let lastQuery = null;
let labExp = 2;
const SRC = {};

const EXP_NAMES = {
  1: 'Array of structs', 2: 'Linked list', 3: 'Stack', 4: 'Circular queue',
  5: 'Binary search tree', 6: 'Graph + BFS', 7: 'Sorting + search', 8: 'Hash table'
};

/* ---------------- wasm bridge ---------------- */
const mem = () => new Uint8Array(E.memory.buffer);
function setIn(str) {
  const b = enc.encode(str.slice(0, 4000));
  const p = E.hh_in();
  const m = mem();
  m.set(b, p);
  m[p + b.length] = 0;
}
function readOut(n) {
  const p = E.hh_out();
  return dec.decode(mem().subarray(p, p + n));
}
function call(fn, ...args) {
  const n = E[fn](...args);
  const txt = readOut(n);
  let obj;
  try { obj = JSON.parse(txt); }
  catch (e) { console.error(fn, txt.slice(0, 400)); throw e; }
  drainTrace();
  return obj;
}
function drainTrace() {
  const n = E.hh_trace();
  const lines = JSON.parse(readOut(n));
  if (lines.length) addLog(lines);
}

/* ---------------- colours ---------------- */
const BANDS = ['--b0', '--b1', '--b2', '--b3', '--b4'];
function band(t) {
  const thr = STATE ? STATE.thr : 45;
  if (t < thr - 7) return 0;
  if (t < thr - 3) return 1;
  if (t < thr) return 2;
  if (t < thr + 2) return 3;
  return 4;
}
const colorOf = (t) => `var(${BANDS[band(t)]})`;

/* ---------------- boot ---------------- */
async function boot() {
  try {
    const [w, d] = await Promise.all([fetch('core.wasm'), fetch('data/season.bin').catch(() => null)]);
    if (!w.ok) throw new Error('core.wasm missing');
    const { instance } = await WebAssembly.instantiate(await w.arrayBuffer(), { env: {} });
    E = instance.exports;
    let data;
    if (d && d.ok) data = new Uint8Array(await d.arrayBuffer());
    else {                                       // hosts that refuse .bin get the same bytes as base64 text
      const t = await fetch('data/season.b64.txt');
      if (!t.ok) throw new Error('data file missing');
      const bin = atob((await t.text()).trim());
      data = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) data[i] = bin.charCodeAt(i);
    }
    mem().set(data, E.hh_data());
    META = call('hh_init', data.length);
    if (!META.ok) throw new Error('data file not recognised');
  } catch (err) {
    $('#loading').textContent = 'Could not start: ' + err.message + '. If you opened index.html straight from disk, serve the folder instead (e.g. python3 -m http.server).';
    return;
  }
  $('#loading').remove();
  buildMap();
  buildControls();
  buildLabNav();
  buildAbout();
  const yi = META.years.indexOf(2024) >= 0 ? META.years.indexOf(2024) : META.years.length - 1;
  setDay(yi, 88);
  select(META.cities.find(c => c.name === 'Delhi').cell);
  loadYears();
  runQuery('above 45 on 28-05-2024', false);
}

/* ---------------- map ---------------- */
function buildMap() {
  const map = $('#map');
  META.cells.forEach(([r, c], i) => {
    const b = document.createElement('button');
    b.className = 'cellb';
    b.style.gridRow = String(31 - r);
    b.style.gridColumn = String(c + 1);
    b.addEventListener('click', () => select(i));
    map.appendChild(b);
    cellEls.push(b);
  });
  const dl = $('#cityList');
  META.cities.forEach(c => { const o = document.createElement('option'); o.value = c.name; dl.appendChild(o); });
}

function renderDay() {
  const s = STATE;
  $('#dateLabel').textContent = s.date;
  $('#mapSub').textContent = `${s.regions.length} heatwave region${s.regions.length === 1 ? '' : 's'} · ${s.stats.hot} cells ≥ ${s.thr.toFixed(1)} °C · ${s.stats.valid} of ${META.cells.length} cells reported`;
  $('#yearSel').value = String(s.yi);
  $('#daySlider').value = String(s.day);
  for (let i = 0; i < cellEls.length; i++) {
    const t10 = s.t[i];
    const el = cellEls[i];
    let cls = 'cellb';
    if (t10 === null) cls += ' nodata';
    else {
      const t = t10 / 10, b = band(t);
      el.style.backgroundColor = `var(${BANDS[b]})`;
      if (b >= 3) cls += b === 4 ? ' glow4' : ' glow3';
    }
    if (s.reg[i] >= 0) cls += ' hw';
    if (i === SEL) cls += ' sel';
    if (highlight.has(i)) cls += ' mark';
    if (showRule && s.rule[i] === '1') cls += ' rule';
    el.className = cls;
    el.setAttribute('aria-label', `${(7.5 + META.cells[i][0]).toFixed(1)}N ${(67.5 + META.cells[i][1]).toFixed(1)}E ${t10 === null ? 'no data' : (t10 / 10).toFixed(1) + ' C'}`);
  }
  // region tags
  const tags = $('#tags');
  tags.innerHTML = '';
  s.regions.slice(0, 5).forEach(rg => {
    const el = cellEls[rg.tag];
    const t = document.createElement('span');
    t.className = 'tag';
    t.textContent = rg.id;
    t.style.left = (el.offsetLeft + el.offsetWidth / 2) + 'px';
    t.style.top = el.offsetTop + 'px';
    tags.appendChild(t);
  });
  // legend
  const thr = s.thr;
  const lab = [`< ${(thr - 7).toFixed(0)}`, `${(thr - 7).toFixed(0)}–${(thr - 3).toFixed(0)}`, `${(thr - 3).toFixed(0)}–${thr.toFixed(1)}`, `${thr.toFixed(1)}–${(thr + 2).toFixed(1)} heatwave`, `≥ ${(thr + 2).toFixed(1)} severe`];
  $('#legend').innerHTML = lab.map((l, k) => `<span><i style="background:var(${BANDS[k]})"></i>${l}</span>`).join('') + '<span><i class="cellb nodata" style="width:13px;height:13px"></i>no reading</span>';
  renderRegions();
  renderFeed();
  renderWatch(s.watch);
  $('#ruleCount').textContent = s.stats.ruleCount;
  setXinfo('.map-card', `BST of ${s.stats.valid} cells, height ${s.stats.treeH} · rangeSearch visited ${s.stats.rangeVisited} nodes · BFS checked ${s.stats.regionChecks} matrix entries`);
}

function renderRegions() {
  const s = STATE;
  const el = $('#regions');
  if (!s.regions.length) { el.innerHTML = '<div class="empty">No cell at or above the threshold on this day.</div>'; return; }
  el.innerHTML = '<div class="tr head"><span>ID</span><span>Cells</span><span>Peak</span><span>Near</span></div>' +
    s.regions.slice(0, 6).map((r, k) => `<div class="tr click" data-k="${k}"><b>${r.id}</b><span>${r.size}</span><span style="color:var(${r.peak >= s.thr + 2 ? '--b4' : '--b3'})">${r.peak.toFixed(1)}°</span><span>${esc(r.near)}</span></div>`).join('') +
    (s.regions.length > 6 ? `<div class="empty small">+ ${s.regions.length - 6} smaller regions</div>` : '');
  el.querySelectorAll('.tr.click').forEach(row => row.addEventListener('click', () => {
    const k = +row.dataset.k;
    highlight = new Set();
    STATE.reg.forEach((v, i) => { if (v === k) highlight.add(i); });
    select(STATE.regions[k].start);
  }));
}

function renderFeed() {
  const f = STATE.feed;
  $('#feedCount').textContent = `count ${f.count} / ${f.cap}`;
  $('#feed').innerHTML = f.slots.map((sl, i) => {
    const front = sl && i === f.front, rear = sl && i === f.rear;
    const tag = front && rear ? '<b>front·rear</b>' : front ? '<b>front</b>' : rear ? '<b>rear</b>' : '';
    return `<div class="slot ${sl ? '' : 'empty'} ${rear ? 'rear' : ''}" title="${sl ? esc(sl.label) + ' ' + sl.year : 'empty slot'}"><span class="si">[${i}] ${tag}</span><span class="sl">${sl ? esc(sl.label) : '<span class="muted">empty</span>'}</span>${sl ? `<span class="sy">${sl.year}</span>` : ''}</div>`;
  }).join('');
  setXinfo('#feed', `front=${f.front} rear=${f.rear} count=${f.count} · enqueue at rear, dequeue at front, indices wrap mod ${f.cap}`);
}

function renderWatch(list) {
  $('#watch').innerHTML = list.length ? list.map((n, i) => `<div class="wnode"><div class="wbox"><b>${esc(n.city)}</b><span class="mono" style="color:${n.value > -90 ? colorOf(n.value) : 'var(--muted)'}">${n.value > -90 ? n.value.toFixed(1) + '°' : 'no data'}</span></div><span class="wnext">${i < list.length - 1 ? '→ next' : '→ NULL'}</span></div>`).join('') : '<div class="empty">The list is empty (head = NULL).</div>';
  if (list.length) setXinfo('#watch', `head → ${list[0].addr} · each node: city[24], value, next pointer`);
}

function setXinfo(sel, text) {
  const host = $(sel).closest('.card') || $(sel);
  let x = host.querySelector(':scope > .xinfo');
  if (!x) { x = document.createElement('div'); x.className = 'xinfo'; host.appendChild(x); }
  x.textContent = text;
}

/* ---------------- day / year / selection ---------------- */
function setDay(yi, d) {
  STATE = call('hh_day', yi, d);
  renderDay();
  if (SEL >= 0) select(SEL, true);
}

function select(i, keepHighlight) {
  if (!keepHighlight && !highlight.has(i)) { /* keep region highlight when clicking inside */ }
  SEL = i;
  const c = call('hh_select', i);
  if (!c.ok) return;
  cellEls.forEach((el, k) => el.classList.toggle('sel', k === i));
  $('#selTitle').textContent = 'Cell near ' + c.near;
  $('#selCoord').textContent = `${c.lat.toFixed(1)}°N ${c.lon.toFixed(1)}°E · grid[${c.row}][${c.col}]`;
  const thr = STATE.thr;
  if (c.valid) {
    $('#selT').textContent = c.t.toFixed(1) + '°C';
    $('#selT').style.color = c.t >= thr ? colorOf(c.t) : 'var(--text)';
    const st = c.t >= thr + 2 ? 'Severe heatwave' : c.t >= thr ? 'Heatwave' : 'Below heatwave level';
    $('#selBadge').textContent = st;
    $('#selBadge').className = 'badge' + (c.t >= thr ? ' hot' : '');
  } else {
    $('#selT').textContent = '—';
    $('#selT').style.color = 'var(--muted)';
    $('#selBadge').textContent = 'No IMD reading today';
    $('#selBadge').className = 'badge';
  }
  $('#selStreak').textContent = c.streak ? `${c.streak} day${c.streak > 1 ? 's' : ''} in a row ≥ ${thr.toFixed(1)}` : '';
  const sp = $('#spark');
  const thrPx = Math.max(0, (thr - 28) * 4);
  sp.innerHTML = `<div class="thr" style="bottom:${thrPx}px"></div>` + c.history.map(h => h.value > -90
    ? `<div class="bar" title="${esc(h.city)}: ${h.value.toFixed(1)} °C" style="height:${Math.max(4, (h.value - 28) * 4)}px;background:${colorOf(h.value)}"></div>`
    : `<div class="bar miss" title="${esc(h.city)}: no reading"></div>`).join('');
  setXinfo('#spark', `Cell struct at ${c.addr} · history head → ${c.history[0] ? c.history[0].addr : 'NULL'} (${c.history.length} nodes via insertBegin)`);
}

async function loadYears() {
  const y = call('hh_years');
  const max = Math.max(1, ...y.years.map(r => r.count));
  $('#years').innerHTML = y.years.map(r => `<button class="yrow ${STATE && r.yi === STATE.yi ? 'on' : ''}" data-yi="${r.yi}" title="peak ${r.peak.toFixed(1)} °C on ${r.peakDate}"><span class="yr">${r.year}</span><span class="track"><span class="fill" style="width:${Math.max(1.5, r.count / max * 100)}%"></span></span><span class="num">${r.count}</span></button>`).join('');
  $('#years').querySelectorAll('.yrow').forEach(b => b.addEventListener('click', () => {
    const yi = +b.dataset.yi, peak = y.years.find(r => r.yi === yi);
    setDay(yi, peak ? peak.peakDay : STATE.day);
    markYear();
  }));
}
function markYear() { document.querySelectorAll('.yrow').forEach(b => b.classList.toggle('on', +b.dataset.yi === STATE.yi)); }

/* ---------------- controls ---------------- */
function buildControls() {
  // tabs
  document.querySelectorAll('.tab').forEach(t => t.addEventListener('click', () => showTab(t.dataset.tab)));
  // year
  const ys = $('#yearSel');
  META.years.forEach((y, i) => { const o = document.createElement('option'); o.value = String(i); o.textContent = y; ys.appendChild(o); });
  ys.addEventListener('change', () => { setDay(+ys.value, STATE.day); markYear(); });
  $('#yPrev').addEventListener('click', () => { if (STATE.yi > 0) { setDay(STATE.yi - 1, STATE.day); markYear(); } });
  $('#yNext').addEventListener('click', () => { if (STATE.yi < META.years.length - 1) { setDay(STATE.yi + 1, STATE.day); markYear(); } });
  // day slider (coalesce rapid input)
  let pending = null;
  $('#daySlider').addEventListener('input', e => {
    pending = +e.target.value;
    requestAnimationFrame(() => { if (pending !== null) { setDay(STATE.yi, pending); pending = null; } });
  });
  $('#playBtn').addEventListener('click', togglePlay);
  // threshold
  $('#thrSlider').addEventListener('change', e => {
    const v = parseFloat(e.target.value);
    $('#thrVal').textContent = v.toFixed(1);
    STATE = call('hh_threshold', Math.round(v * 10));
    renderDay();
    if (SEL >= 0) select(SEL, true);
    loadYears();
  });
  $('#thrSlider').addEventListener('input', e => { $('#thrVal').textContent = parseFloat(e.target.value).toFixed(1); });
  // rule
  $('#ruleForm').addEventListener('submit', e => {
    e.preventDefault();
    setIn($('#ruleInput').value);
    const r = call('hh_rule');
    if (!r.ok) { $('#ruleErr').textContent = r.error; return; }
    $('#ruleErr').textContent = '';
    $('#rulePostfix').textContent = r.postfix;
    STATE = r.state;
    renderDay();
  });
  $('#ruleShow').addEventListener('change', e => { showRule = e.target.checked; renderDay(); });
  // watchlist
  document.querySelectorAll('[data-w]').forEach(b => b.addEventListener('click', () => {
    const op = b.dataset.w, city = $('#watchCity').value.trim(), key = $('#watchKey').value.trim();
    let args;
    if (op === 'insertBegin') { if (!city) return werr('type a city first'); args = [op, city]; }
    else if (op === 'insertAfter') { if (!city || !key) return werr('insertAfter needs a city and an existing city'); args = [op, key, city]; }
    else { if (!key) return werr('deleteBefore needs an existing city'); args = [op, key]; }
    setIn(args.join('|'));
    const r = call('hh_watch');
    werr(r.ok ? '' : r.msg);
    renderWatch(r.watch);
  }));
  // x-ray & log
  $('#xrayBtn').addEventListener('click', () => {
    const on = !document.body.classList.contains('xray');
    document.body.classList.toggle('xray', on);
    $('#xrayBtn').classList.toggle('on', on);
    $('#xrayBtn').setAttribute('aria-pressed', String(on));
  });
  document.querySelectorAll('[data-lab]').forEach(card => card.addEventListener('click', e => {
    if (!document.body.classList.contains('xray')) return;
    if (e.target.closest('button, input, select, a, label')) return;
    openLab(+card.dataset.lab);
  }));
  $('#logBtn').addEventListener('click', () => {
    const hidden = $('#logPanel').classList.toggle('hidden');
    $('#logBtn').classList.toggle('on', !hidden);
    $('#logBtn').setAttribute('aria-pressed', String(!hidden));
  });
  $('#logClear').addEventListener('click', () => { $('#logBody').innerHTML = ''; });
  const chips = ['all', '1', '2', '3', '4', '5', '6', '7', '8'];
  $('#logChips').innerHTML = chips.map(k => `<button class="chip ${k === 'all' ? 'on' : ''}" data-f="${k}" title="${k === 'all' ? 'All experiments' : EXP_NAMES[k]}">${k === 'all' ? 'All' : 'Exp ' + k}</button>`).join('');
  $('#logChips').querySelectorAll('.chip').forEach(c => c.addEventListener('click', () => {
    logFilter = c.dataset.f;
    $('#logChips').querySelectorAll('.chip').forEach(x => x.classList.toggle('on', x === c));
    $('#logBody').querySelectorAll('.ll').forEach(l => { l.style.display = (logFilter === 'all' || l.dataset.e === logFilter) ? '' : 'none'; });
  }));
  // search
  $('#quickForm').addEventListener('submit', e => { e.preventDefault(); const q = $('#quickInput').value.trim() || 'above 45 on 28-05-2024'; $('#searchInput').value = q; showTab('search'); runQuery(q, true); });
  $('#searchForm').addEventListener('submit', e => { e.preventDefault(); runQuery($('#searchInput').value, true); });
  const tries = ['above 45 on 28-05-2024', 'hottest 10 on 10-06-2019', 'hottest years', 'find delhi', 'spread from churu on 28-05-2024', 'where T >= 46 && days >= 3 on 30-05-2024'];
  $('#tries').innerHTML = '<span>try</span>' + tries.map(t => `<button type="button">${esc(t)}</button>`).join('');
  $('#tries').querySelectorAll('button').forEach(b => b.addEventListener('click', () => { $('#searchInput').value = b.textContent; runQuery(b.textContent, true); }));
  $('#qShow').addEventListener('click', () => {
    if (!lastQuery) return;
    highlight = new Set(lastQuery.cells || []);
    showTab('tracker');
    renderDay();
    if (lastQuery.select !== undefined) select(lastQuery.select, true);
    else if (lastQuery.cells && lastQuery.cells.length) select(lastQuery.cells[lastQuery.cells.length - 1], true);
  });
  // modal
  $('#modalClose').addEventListener('click', () => { $('#srcModal').hidden = true; });
  $('#srcModal').addEventListener('click', e => { if (e.target.id === 'srcModal') $('#srcModal').hidden = true; });
  window.addEventListener('resize', () => STATE && renderDay());
}
function werr(m) { $('#watchErr').textContent = m; }

function togglePlay() {
  playing = !playing;
  $('#playBtn').textContent = playing ? '❚❚' : '▶';
  $('#playBtn').setAttribute('aria-label', playing ? 'Pause' : 'Play the season');
  clearInterval(playTimer);
  if (playing) {
    if (STATE.day >= 121) setDay(STATE.yi, 0);
    playTimer = setInterval(() => {
      if (STATE.day >= 121) { togglePlay(); return; }
      setDay(STATE.yi, STATE.day + 1);
    }, 320);
  }
}

function showTab(name) {
  document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.tab === name));
  document.querySelectorAll('.tabpane').forEach(p => p.classList.toggle('active', p.id === 'tab-' + name));
  if (name === 'tracker' && STATE) requestAnimationFrame(renderDay);
  if (name === 'lab' && !$('#labPanel').dataset.ready) openLab(labExp);
}

/* ---------------- log ---------------- */
const FN_FILE = {
  readGrid: 'grid.c', getCell: 'grid.c', updateCell: 'grid.c', cellByIndex: 'grid.c', data_parse: 'grid.c',
  insertBegin: 'list.c', insertAfter: 'list.c', deleteBefore: 'list.c', display: 'list.c', createSLL: 'list.c', poolAlloc: 'list.c', freeList: 'list.c',
  push: 'stack.c', pop: 'stack.c', peek: 'stack.c', isEmpty: 'stack.c', toPostfix: 'stack.c', evalPostfix: 'stack.c', tokenize: 'stack.c',
  createQueue: 'queue.c', enqueue: 'queue.c', dequeue: 'queue.c', isFull: 'queue.c', isQEmpty: 'queue.c',
  bstInsert: 'bst.c', bstSearch: 'bst.c', bstDelete: 'bst.c', inorder: 'bst.c', preorder: 'bst.c', postorder: 'bst.c', levelOrder: 'bst.c', rangeSearch: 'bst.c', bstFree: 'bst.c',
  graphInit: 'graph.c', addEdge: 'graph.c', bfs: 'graph.c',
  quickSort: 'sort.c', mergeSort: 'sort.c', mergeRec: 'sort.c', insertionSort: 'sort.c', binarySearch: 'sort.c',
  hashInit: 'hash.c', hashKey: 'hash.c', hashInsert: 'hash.c', hashSearch: 'hash.c', hashDelete: 'hash.c'
};
const FN_ALIAS = { search: 'hashSearch', insert: 'hashInsert', hash: 'hashKey' };

function fnOfLine(t) {
  const m = /^([A-Za-z_]+)/.exec(t);
  if (!m) return null;
  const f = FN_ALIAS[m[1]] || m[1];
  return FN_FILE[f] ? f : null;
}

function addLog(lines) {
  const body = $('#logBody');
  const frag = document.createDocumentFragment();
  for (const l of lines) {
    const row = document.createElement('div');
    row.className = 'll';
    row.dataset.e = String(l.e);
    if (logFilter !== 'all' && logFilter !== String(l.e)) row.style.display = 'none';
    const fn = fnOfLine(l.t);
    row.innerHTML = `<span class="le">[EXP${l.e}]</span><span class="lt ${fn ? 'fn' : ''}">${esc(l.t)}</span>`;
    if (fn) row.querySelector('.lt').addEventListener('click', () => openSource(fn, true));
    frag.prepend(row);
  }
  body.prepend(frag);
  while (body.childElementCount > 400) body.lastElementChild.remove();
}

/* ---------------- search ---------------- */
function runQuery(q, jump) {
  setIn(q);
  const r = call('hh_query');
  if (!r.ok) { $('#searchErr').textContent = r.error; return; }
  $('#searchErr').textContent = '';
  lastQuery = r;
  $('#qTitle').textContent = r.title;
  $('#qMeta').textContent = r.meta;
  const cols = r.cols.filter(c => c !== '');
  const tpl = `repeat(${cols.length}, minmax(0, 1fr))`;
  $('#qRows').style.setProperty('--cols', tpl);
  $('#qRows').innerHTML = `<div class="tr head">${cols.map(c => `<span>${esc(c)}</span>`).join('')}</div>` +
    r.rows.map(row => `<div class="tr">${row.slice(0, cols.length).map(v => `<span>${esc(v)}</span>`).join('')}</div>`).join('');
  $('#qMore').textContent = r.more ? `+ ${r.more} more` : '';
  $('#qShow').hidden = !(r.cells && r.cells.length);
  $('#qPlan').innerHTML = r.plan.map((p, i) => `<li><span class="n">${i + 1}</span><div><div class="t">${esc(p.title)}<span class="e">${esc(p.exp)}</span></div><div class="d">${esc(p.detail)}</div></div></li>`).join('');
  const rc = $('#qRaceCard');
  if (r.race && r.race.length) {
    rc.hidden = false;
    const max = Math.max(...r.race.map(x => x.count), 1);
    const colors = ['var(--b1)', 'var(--b2)', 'var(--accent)'];
    $('#qRace').innerHTML = r.race.map((x, i) => `<div class="race"><div class="rl"><span>${esc(x.name)} <span class="mono muted small">${esc(x.exp)}</span></span><span>${x.count.toLocaleString('en-IN')}</span></div><div class="rt"><div class="rf" data-w="${Math.max(2, Math.log10(x.count + 1) / Math.log10(max + 1) * 100)}" style="background:${colors[i % 3]}"></div></div></div>`).join('');
    requestAnimationFrame(() => requestAnimationFrame(() => $('#qRace').querySelectorAll('.rf').forEach(b => { b.style.width = b.dataset.w + '%'; })));
    $('#qNote').textContent = r.note || 'Counts are measured inside the C code as it runs.';
  } else rc.hidden = true;
  const sc = $('#qStepsCard');
  if (r.steps) {
    sc.hidden = false;
    $('#qSteps').style.setProperty('--cols', 'minmax(0,.6fr) minmax(0,1.6fr) minmax(0,1fr) minmax(0,1.6fr)');
    $('#qSteps').innerHTML = '<div class="tr head"><span>token</span><span>action</span><span>stack</span><span>output</span></div>' +
      r.steps.map(s => `<div class="tr"><span>${esc(s.tok)}</span><span>${esc(s.action)}</span><span>${esc(s.stack)}</span><span>${esc(s.out)}</span></div>`).join('');
  } else sc.hidden = true;
  if (r.jump && STATE && (r.jump.yi !== STATE.yi || r.jump.day !== STATE.day)) {
    STATE = call('hh_state');
    renderDay();
    markYear();
  }
  if (jump && r.select !== undefined) select(r.select, true);
}

/* ---------------- lab ---------------- */
const LAB = {
  1: { title: 'Exp 1 · Array of structures and pointers', fns: ['getCell', 'updateCell', 'readGrid'], init: '1|get|28.5|77.5' },
  2: { title: 'Exp 2 · Singly linked list', fns: ['insertBegin', 'insertAfter', 'deleteBefore', 'display'], init: '2|reset' },
  3: { title: 'Exp 3 · Stack (linked list) and infix → postfix', fns: ['toPostfix', 'evalPostfix', 'push', 'pop', 'peek'], init: '3|convert|T >= 45 && days >= 2|46|2' },
  4: { title: 'Exp 4 · Static circular queue (counter method)', fns: ['enqueue', 'dequeue', 'createQueue', 'isFull'], init: '4|reset' },
  5: { title: 'Exp 5 · Binary search tree', fns: ['bstInsert', 'bstSearch', 'bstDelete', 'inorder', 'preorder', 'postorder', 'levelOrder'], init: '5|reset' },
  6: { title: 'Exp 6 · Graph (adjacency matrix) and BFS', fns: ['bfs', 'addEdge', 'graphInit'], init: '6|reset' },
  7: { title: 'Exp 7 · Sorting and binary search', fns: ['quickSort', 'mergeSort', 'insertionSort', 'binarySearch'], init: '7|load' },
  8: { title: 'Exp 8 · Hash table (circular array, linear probing)', fns: ['hashInsert', 'hashSearch', 'hashDelete', 'hashKey'], init: '8|reset' }
};

function buildLabNav() {
  $('#labNav').innerHTML = Object.keys(LAB).map(k => `<button data-x="${k}" class="${+k === labExp ? 'on' : ''}"><span class="k">EXP ${k} · ${LAB[k].fns.length} functions</span><span class="v">${EXP_NAMES[k]}</span></button>`).join('');
  $('#labNav').querySelectorAll('button').forEach(b => b.addEventListener('click', () => openLab(+b.dataset.x)));
}

function openLab(x) {
  labExp = x;
  $('#labPanel').dataset.ready = '1';      // set first: showTab('lab') opens the lab only when not ready
  showTab('lab');
  $('#labNav').querySelectorAll('button').forEach(b => b.classList.toggle('on', +b.dataset.x === x));
  labRun(LAB[x].init, LAB[x].fns[0]);
}

function labRun(cmd, fn) {
  setIn(cmd);
  const r = call('hh_lab');
  renderLab(r, cmd.split('|')[1]);
  if (fn) openSource(fn, false);
}

function opsHTML(fields, buttons) {
  return `<div class="lab-ops">${fields.map(f => `<label>${f.label}<input class="field mono ${f.wide ? 'wide' : ''}" id="${f.id}" value="${esc(f.value || '')}"></label>`).join('')}${buttons.map(b => `<button class="btn ${b.on ? 'on' : ''}" data-op="${b.op}">${b.text}</button>`).join('')}</div>`;
}
const v = id => ($('#' + id) ? $('#' + id).value.trim() : '');

function renderLab(r, op) {
  const x = r.exp, P = $('#labPanel');
  if (P.dataset.exp !== String(x)) { P.innerHTML = ''; P.dataset.exp = String(x); }   // new experiment: start from default inputs
  let html = `<h1>${LAB[x].title}</h1>`;
  const msg = `<div class="lab-msg ${r.ok ? 'ok' : 'bad'}">${r.ok ? (op ? '✓ ' + esc(op) : '') : '✗ ' + esc(r.msg)}</div>`;
  let viz = '';
  if (x === 1) {
    html += opsHTML([{ label: 'lat', id: 'l1', value: r.cell ? r.cell.lat.toFixed(1) : '28.5' }, { label: 'lon', id: 'l2', value: r.cell ? r.cell.lon.toFixed(1) : '77.5' }, { label: 'new tmax (copy)', id: 'l3', value: '50.0' }],
      [{ op: 'get', text: 'getCell()', on: op === 'get' }, { op: 'update', text: 'copy + update', on: op === 'update' }]);
    if (r.cell) {
      const c = r.cell;
      viz = `<div class="structbox"><span class="k">struct Cell at</span><span>${c.addr}</span><span class="k">row, col</span><span>${c.row}, ${c.col}</span><span class="k">lat, lon</span><span>${c.lat.toFixed(1)}, ${c.lon.toFixed(1)}</span><span class="k">tmax</span><span>${c.valid ? c.tmax.toFixed(1) + ' °C' : '— (IMD had no reading)'}</span><span class="k">valid</span><span>${c.valid}</span><span class="k">index</span><span>${c.index}</span></div>
      <p class="mono small muted">&amp;grid[${c.row}][${c.col}] = ${r.base} + (${c.row} × 31 + ${c.col}) × sizeof(Cell) = ${r.base} + ${(c.row * 31 + c.col) * r.size} bytes = ${c.addr} &nbsp;(sizeof(Cell) = ${r.size})</p>
      ${r.copy ? `<p class="mono small">Copy at ${r.copy.addr} now holds tmax = ${r.copy.tmax.toFixed(1)}. grid[${c.row}][${c.col}] still holds ${c.valid ? c.tmax.toFixed(1) : '—'}: changing a copy never changes the array.</p>` : ''}`;
    }
  } else if (x === 2) {
    html += opsHTML([{ label: 'new city', id: 'l1', value: v('l1') || 'Jaipur' }, { label: 'existing city', id: 'l2', value: v('l2') || 'Delhi' }],
      ['insertBegin', 'insertAfter', 'deleteBefore', 'display'].map(o => ({ op: o, text: o + '()', on: op === o })).concat([{ op: 'reset', text: 'reset' }]));
    viz = `<div class="chain"><span class="headp">head →</span>${r.list.map(n => `<div class="node"><div class="nv"><b>${esc(n.city)}</b><small>${n.addr}</small></div><div class="nx">next</div></div><span class="arrow">→</span>`).join('')}<span class="nullp">NULL</span></div>
      <div class="table mono small" style="--cols:repeat(4,minmax(0,1fr));margin-top:14px"><div class="tr head"><span>index</span><span>city</span><span>node at</span><span>next</span></div>${r.list.map((n, i) => `<div class="tr"><span>${i}</span><span>${esc(n.city)}</span><span>${n.addr}</span><span>${n.next}</span></div>`).join('')}</div>`;
  } else if (x === 3) {
    html += opsHTML([{ label: 'infix expression', id: 'l1', value: r.postfix !== undefined ? v('l1') || 'T >= 45 && days >= 2' : 'T >= 45 && days >= 2', wide: true }, { label: 'T', id: 'l2', value: v('l2') || '46' }, { label: 'days', id: 'l3', value: v('l3') || '2' }],
      [{ op: 'convert', text: 'convert + evaluate', on: op === 'convert' }]) +
      opsHTML([{ label: 'token', id: 'l4', value: v('l4') || '(' }], [{ op: 'push', text: 'push()' }, { op: 'pop', text: 'pop()' }, { op: 'peek', text: 'peek()' }, { op: 'reset', text: 'clear' }]);
    viz = (r.steps ? `<p class="mono">postfix: <span class="code">${esc(r.postfix)}</span> &nbsp; → value ${r.value === null ? 'error' : r.value}</p>
      <div class="table mono small" style="--cols:minmax(0,.5fr) minmax(0,1.5fr) minmax(0,1fr) minmax(0,1.5fr)"><div class="tr head"><span>token</span><span>action</span><span>stack</span><span>output</span></div>${r.steps.map(s => `<div class="tr"><span>${esc(s.tok)}</span><span>${esc(s.action)}</span><span>${esc(s.stack)}</span><span>${esc(s.out)}</span></div>`).join('')}</div>` : '') +
      `<h2 style="margin-top:16px">Your stack (top first)</h2><div class="stackviz">${r.stack.length ? r.stack.map((s, i) => `<div class="sitem"><span>${esc(s.v)}</span><span class="muted small">${i === 0 ? 'top · ' : ''}${s.addr}</span></div>`).join('') : '<span class="muted mono small">empty (top = NULL)</span>'}</div>`;
  } else if (x === 4) {
    html += opsHTML([{ label: 'value', id: 'l1', value: v('l1') || '45' }], [{ op: 'enqueue', text: 'enqueue()', on: op === 'enqueue' }, { op: 'dequeue', text: 'dequeue()', on: op === 'dequeue' }, { op: 'reset', text: 'reset' }]);
    const R = 115;
    viz = `<div class="ring">${r.slots.map((s, i) => {
      const a = (-90 + i * 360 / r.cap) * Math.PI / 180;
      const isF = s !== null && i === r.front, isR = s !== null && i === r.rear;
      return `<div class="q ${s === null ? 'emp' : ''} ${isF ? 'front' : ''} ${isR ? 'rear' : ''}" style="left:${150 + R * Math.cos(a)}px;top:${150 + R * Math.sin(a)}px"><span class="i">[${i}]</span><span class="v">${s === null ? '·' : s}</span>${isF || isR ? `<span class="f">${isF && isR ? 'front rear' : isF ? 'front' : 'rear'}</span>` : ''}</div>`;
    }).join('')}<div class="center">front ${r.front}<br>rear ${r.rear}<br>count ${r.count}/${r.cap}</div></div>`;
  } else if (x === 5) {
    html += opsHTML([{ label: 'key (°C)', id: 'l1', value: v('l1') || '44.0' }], ['insert', 'search', 'delete'].map(o => ({ op: o, text: o + '()', on: op === o })).concat([{ op: 'reset', text: 'reset' }]));
    const n = r.nodes, W = Math.max(520, n.length * 54), byId = {};
    n.forEach(nd => { byId[nd.id] = nd; });
    const px = nd => 30 + nd.x * ((W - 60) / Math.max(1, n.length - 1 || 1));
    const py = nd => 16 + nd.depth * 52;
    let lines = '';
    n.forEach(nd => [nd.l, nd.r].forEach(ch => {
      if (ch < 0 || !byId[ch]) return;
      const c = byId[ch], x1 = px(nd), y1 = py(nd) + 26, x2 = px(c), y2 = py(c), dx = x2 - x1, dy = y2 - y1;
      lines += `<div class="tline" style="left:${x1}px;top:${y1}px;width:${Math.hypot(dx, dy)}px;transform:rotate(${Math.atan2(dy, dx)}rad)"></div>`;
    }));
    viz = `<div class="tree" style="width:${W}px;height:${Math.max(120, r.height * 52 + 20)}px">${lines}${n.map(nd => `<div class="tnode ${r.found === nd.id ? 'hit' : ''}" style="left:${px(nd)}px;top:${py(nd)}px">${nd.key.toFixed(1)}</div>`).join('')}</div>
      <div class="table mono small" style="--cols:minmax(0,.4fr) minmax(0,2fr)"><div class="tr"><span>in-order</span><span>${r.in}</span></div><div class="tr"><span>pre-order</span><span>${r.pre}</span></div><div class="tr"><span>post-order</span><span>${r.post}</span></div><div class="tr"><span>level-order</span><span>${r.level}</span></div><div class="tr"><span>height</span><span>${r.height}</span></div></div>`;
  } else if (x === 6) {
    html += opsHTML([{ label: 'u', id: 'l1', value: v('l1') || '1' }, { label: 'v', id: 'l2', value: v('l2') || '2' }, { label: 'BFS start (0–7)', id: 'l3', value: v('l3') || '0' }],
      [{ op: 'addEdge', text: 'addEdge()', on: op === 'addEdge' }, { op: 'bfs', text: 'bfs()', on: op === 'bfs' }, { op: 'reset', text: 'reset' }]);
    viz = `<table class="matrix"><tr><th></th>${r.names.map((nm, i) => `<th title="${esc(nm)}">${i}</th>`).join('')}</tr>${r.matrix.map((row, i) => `<tr><th>${i} ${esc(r.names[i].slice(0, 6))}</th>${[...row].map(ch => `<td class="${ch === '1' ? 'one' : ''}">${ch}</td>`).join('')}</tr>`).join('')}</table>
      ${r.order.length ? `<h2 style="margin-top:16px">BFS order (level)</h2><div class="levels">${r.order.map((o, i) => `<span class="lvl" style="animation-delay:${i * 90}ms">${esc(r.names[o])} · L${r.levels[i]}</span>`).join('')}</div>` : ''}`;
  } else if (x === 7) {
    html += opsHTML([{ label: 'search key', id: 'l1', value: v('l1') || (r.arr[3] !== undefined ? r.arr[3].toFixed(1) : '40') }],
      [{ op: 'quick', text: 'quickSort()', on: op === 'quick' }, { op: 'merge', text: 'mergeSort()', on: op === 'merge' }, { op: 'insertion', text: 'insertionSort()', on: op === 'insertion' }, { op: 'search', text: 'binarySearch()', on: op === 'search' }, { op: 'shuffle', text: 'unsort' }, { op: 'load', text: 'reload' }]);
    viz = `<div class="arr">${r.arr.map((a, i) => `<div class="ai"><b class="${r.found === i ? 'hit' : ''}">${a.toFixed(1)}</b><span>[${i}]</span></div>`).join('')}</div>
      <p class="mono small muted">${r.sorted ? `sorted with ${r.cmp} comparisons` : 'not sorted yet: temperatures from today\'s grid, in grid order'}${r.found >= 0 ? ` · found at index ${r.found} in ${r.steps} steps` : r.found === -1 ? ` · not found (${r.steps} steps)` : ''}</p>`;
  } else if (x === 8) {
    html += opsHTML([{ label: 'city', id: 'l1', value: v('l1') || 'delhi' }], ['insert', 'search', 'delete'].map(o => ({ op: o, text: 'hash' + o[0].toUpperCase() + o.slice(1) + '()', on: op === o })).concat([{ op: 'preset', text: 'add 6 cities' }, { op: 'reset', text: 'reset' }]));
    viz = `<div class="slots">${r.slots.map((s, i) => `<div class="hslot ${s.state === 0 ? 'empty' : s.state === 2 ? 'del' : ''} ${i === r.last.slot ? 'last' : ''}"><span class="i">[${i}]</span><span class="k">${s.state === 1 ? esc(s.key) : s.state === 2 ? 'deleted' : '·'}</span><span class="h">${s.state === 1 ? 'home ' + s.home + (s.home !== i ? ' → probed' : '') : ''}</span></div>`).join('')}</div>
      <p class="mono small muted">${r.last.key ? `"${esc(r.last.key)}": hash → slot ${r.last.home}, ended at ${r.last.slot < 0 ? '—' : r.last.slot} after ${r.last.probes} probe${r.last.probes === 1 ? '' : 's'}` : 'size 11 · hash = djb2(name) % 11 · collisions move to the next slot, wrapping around'} · ${r.count} keys</p>`;
  }
  P.innerHTML = html + msg + `<div class="viz">${viz}</div>`;
  P.querySelectorAll('[data-op]').forEach(b => b.addEventListener('click', () => labOp(x, b.dataset.op)));
}

function labOp(x, op) {
  const fnFor = {
    1: { get: 'getCell', update: 'updateCell' },
    2: { insertBegin: 'insertBegin', insertAfter: 'insertAfter', deleteBefore: 'deleteBefore', display: 'display', reset: 'insertBegin' },
    3: { convert: 'toPostfix', push: 'push', pop: 'pop', peek: 'peek', reset: 'push' },
    4: { enqueue: 'enqueue', dequeue: 'dequeue', reset: 'createQueue' },
    5: { insert: 'bstInsert', search: 'bstSearch', delete: 'bstDelete', reset: 'bstInsert' },
    6: { addEdge: 'addEdge', bfs: 'bfs', reset: 'graphInit' },
    7: { quick: 'quickSort', merge: 'mergeSort', insertion: 'insertionSort', search: 'binarySearch', shuffle: 'quickSort', load: 'quickSort' },
    8: { insert: 'hashInsert', search: 'hashSearch', delete: 'hashDelete', reset: 'hashInit', preset: 'hashInsert' }
  }[x][op];
  let cmd;
  if (x === 1) cmd = op === 'get' ? `1|get|${v('l1')}|${v('l2')}` : `1|update|${v('l1')}|${v('l2')}|${v('l3')}`;
  else if (x === 2) cmd = op === 'insertBegin' ? `2|insertBegin|${v('l1')}` : op === 'insertAfter' ? `2|insertAfter|${v('l2')}|${v('l1')}` : op === 'deleteBefore' ? `2|deleteBefore|${v('l2')}` : `2|${op}`;
  else if (x === 3) cmd = op === 'convert' ? `3|convert|${v('l1')}|${v('l2')}|${v('l3')}` : op === 'push' ? `3|push|${v('l4')}` : `3|${op}`;
  else if (x === 4) cmd = op === 'enqueue' ? `4|enqueue|${v('l1')}` : `4|${op}`;
  else if (x === 5) cmd = op === 'reset' ? '5|reset' : `5|${op}|${v('l1')}`;
  else if (x === 6) cmd = op === 'addEdge' ? `6|addEdge|${v('l1')}|${v('l2')}` : op === 'bfs' ? `6|bfs|${v('l3')}` : '6|reset';
  else if (x === 7) cmd = op === 'search' ? `7|search|${v('l1')}` : `7|${op}`;
  else if (x === 8) {
    if (op === 'preset') {
      ['delhi', 'jaipur', 'churu', 'nagpur', 'patna', 'bhopal'].forEach(c => { setIn(`8|insert|${c}`); call('hh_lab'); });
      cmd = '8|search|bhopal';
    } else cmd = op === 'reset' ? '8|reset' : `8|${op}|${v('l1')}`;
  }
  if (cmd.includes('||') || /\|$/.test(cmd)) { /* empty field */ }
  labRun(cmd, fnFor);
}

/* ---------------- C source viewer ---------------- */
async function getSrc(file) {
  if (!SRC[file]) {
    try { SRC[file] = await (await fetch('src/' + file)).text(); }
    catch (e) { SRC[file] = '/* source not available */'; }
  }
  return SRC[file];
}

function extractFn(text, name) {
  const lines = text.split('\n');
  const re = new RegExp('^[A-Za-z].*\\b' + name + '\\s*\\(');
  for (let i = 0; i < lines.length; i++) {
    if (re.test(lines[i]) && !/;\s*$/.test(lines[i])) {
      let depth = 0, started = false, out = [];
      let j = i;
      while (j > 0 && /^\s*(\/\*|\*)/.test(lines[j - 1])) j--;    // include comment block above
      for (let k = j; k < lines.length; k++) {
        out.push(lines[k]);
        if (k >= i) {
          for (const ch of lines[k]) { if (ch === '{') { depth++; started = true; } if (ch === '}') depth--; }
          if (started && depth === 0) break;
        }
      }
      return out.join('\n');
    }
  }
  return '/* ' + name + ' not found */';
}

function highlightC(code) {
  const KW = /\b(int|float|void|return|if|else|while|for|struct|static|char|const|unsigned|long|typedef|do|break|sizeof)\b/;
  return esc(code).replace(/(\/\*[\s\S]*?\*\/|\/\/[^\n]*)|\b(int|float|void|return|if|else|while|for|struct|static|char|const|unsigned|long|typedef|do|break|sizeof)\b/g,
    (m, cm, kw) => cm ? `<span class="cm">${cm}</span>` : KW.test(kw) ? `<span class="kw">${kw}</span>` : m);
}

async function openSource(fn, modal) {
  const file = FN_FILE[fn];
  if (!file) return;
  const code = extractFn(await getSrc(file), fn);
  if (modal) {
    $('#modalTitle').textContent = `core/${file} · ${fn}()`;
    $('#modalCode').innerHTML = highlightC(code);
    $('#srcModal').hidden = false;
  } else {
    $('#srcName').textContent = `core/${file} · ${fn}()`;
    $('#srcCode').innerHTML = highlightC(code);
  }
}

/* ---------------- about ---------------- */
function buildAbout() {
  const rows = [
    ['1', 'Array of structs', 'Cell grid[31][31] holds the day; getCell() returns a pointer'],
    ['2', 'Linked list', 'Watchlist (insertBegin / insertAfter / deleteBefore) and each cell\'s 14-day history'],
    ['3', 'Stack', 'Alert rule and "where" searches: infix → postfix → evaluate'],
    ['4', 'Circular queue', 'Incoming-day feed, the BFS queue, and this function log'],
    ['5', 'Binary search tree', 'Cells keyed by temperature; range search for "above 45"'],
    ['6', 'Graph + BFS', 'Adjacency matrix of touching cells; BFS finds heatwave regions and spread'],
    ['7', 'Sorting + search', 'Regions, cells and years ranked; binary search in the race'],
    ['8', 'Hash table', 'City name → grid cell in one probe (usually), linear probing on collisions']
  ];
  $('#expTable').style.setProperty('--cols', 'minmax(0,.3fr) minmax(0,1fr) minmax(0,2.4fr)');
  $('#expTable').innerHTML = rows.map(r => `<div class="tr"><span class="mono">${r[0]}</span><b>${r[1]}</b><span class="muted">${r[2]}</span></div>`).join('');
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

boot();
