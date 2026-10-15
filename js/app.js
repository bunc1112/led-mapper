/* UI glue. */
(function () {
  'use strict';
  const VERSION = '1.0';
  const STORE_KEY = 'ledmapper.state.v1';
  const $ = id => document.getElementById(id);

  const DEFAULTS = {
    lang: (navigator.language || 'en').toLowerCase().startsWith('sr') ? 'sr'
      : /^(hr|bs)/.test((navigator.language || '').toLowerCase()) ? 'hr' : 'en',
    screenW: 6, screenH: 4, cabPreset: '0.5x0.5', cabW: 0.5, cabH: 0.5, cabKg: 8.4, cabA: 0.25,
    mixedRows: true, partialRowPos: 'bottom', partialKg: 0, partialA: 0, hung: false, rigPoints: 'auto',
    pitch: 'P3.9', customPx: 128, refresh: 60, headroom: 0, portsPerSender: 4,
    orientation: 'h', pattern: 'snake', startV: 'bottom', startH: 'left',
    portLen: 5, shortLen: 0.5, voltage: 230, projectName: '', printer: false, optionIndex: 0,
  };
  const NUM_FIELDS = ['screenW', 'screenH', 'cabW', 'cabH', 'cabKg', 'cabA', 'customPx', 'portLen', 'shortLen', 'voltage'];
  const OPT_NUM_FIELDS = ['partialKg', 'partialA']; // empty = automatic
  // Typical weight / current per cabinet size; picking a size fills these in (still editable).
  const CAB_DEFAULTS = {
    '0.5x0.5': { cabKg: 8.4, cabA: 0.25, partialKg: 0, partialA: 0 },
    '0.5x1': { cabKg: 14, cabA: 0.5, partialKg: 8.4, partialA: 0.25 },
    '1x0.5': { cabKg: 14, cabA: 0.5, partialKg: 0, partialA: 0 },
  };
  const SEL_FIELDS = ['cabPreset', 'partialRowPos', 'rigPoints', 'pitch', 'headroom', 'portsPerSender', 'orientation', 'pattern', 'startV', 'startH'];
  const CHK_FIELDS = ['mixedRows', 'hung', 'printer'];

  let S = Object.assign({}, DEFAULTS);
  try { S = Object.assign(S, JSON.parse(localStorage.getItem(STORE_KEY) || '{}')); } catch (e) { /* ignore */ }
  let current = null; // last computed model

  const T = k => (I18N[S.lang] || I18N.en)[k] || I18N.en[k] || k;
  const locale = () => ({ en: 'en-US', hr: 'hr-HR', sr: 'sr-Latn-RS' }[S.lang] || 'en-US');
  const fmt = (v, d) => Number(v).toLocaleString(locale(), { minimumFractionDigits: d || 0, maximumFractionDigits: d || 0 });
  const fmtm = v => fmt(v, 2);

  function save() { try { localStorage.setItem(STORE_KEY, JSON.stringify(S)); } catch (e) { /* ignore */ } }

  function toast(msg, action) {
    const el = $('toast');
    el.textContent = msg;
    if (action) {
      const b = document.createElement('button');
      b.textContent = action.label; b.addEventListener('click', action.run);
      el.appendChild(b);
    }
    el.classList.add('show');
    clearTimeout(toast.t); toast.t = setTimeout(() => el.classList.remove('show'), action ? 5000 : 2600);
  }

  /* ---------- static option lists ---------- */
  function pxFor(pitchId) {
    const p = Engine.PITCH_PRESETS.find(x => x.id === pitchId);
    return p ? Math.round(S.cabW * 1000 / p.pitch) : S.customPx;
  }
  function fillPitch() {
    const sel = $('pitch');
    sel.innerHTML = '';
    const groups = { fine: T('grpFine'), indoor: T('grpIndoor'), outdoor: T('grpOutdoor') };
    Object.keys(groups).forEach(gk => {
      const og = document.createElement('optgroup'); og.label = groups[gk];
      Engine.PITCH_PRESETS.filter(p => p.group === gk).forEach(p => {
        const o = document.createElement('option');
        o.value = p.id;
        o.textContent = `${pxFor(p.id)} px (${p.id} · ${p.pitch} mm, ${Math.round(S.cabW * 1000)}mm)`;
        og.appendChild(o);
      });
      sel.appendChild(og);
    });
    const o = document.createElement('option'); o.value = 'custom'; o.textContent = T('custom'); sel.appendChild(o);
    if (!Engine.PITCH_PRESETS.some(p => p.id === S.pitch) && S.pitch !== 'custom') S.pitch = 'P3.9';
    sel.value = S.pitch;
  }
  function fillHeadroom() {
    const sel = $('headroom'); sel.innerHTML = '';
    [0, 5, 10, 15, 20, 25, 50].forEach(h => {
      const o = document.createElement('option'); o.value = h;
      const cap = Engine.portCapacity(S.refresh, h);
      o.textContent = `${h}% (${fmt(cap)} ${T('pxPerPort')})${h === 50 ? ' — ' + T('hr10bit') : ''}`;
      sel.appendChild(o);
    });
    sel.value = String(S.headroom);
  }
  function fillRig() {
    const sel = $('rigPoints'); sel.innerHTML = '';
    const o = document.createElement('option'); o.value = 'auto'; o.textContent = T('auto'); sel.appendChild(o);
    for (let i = 2; i <= 12; i++) { const p = document.createElement('option'); p.value = String(i); p.textContent = String(i); sel.appendChild(p); }
    sel.value = String(S.rigPoints);
  }

  function applyLang() {
    document.documentElement.lang = S.lang;
    document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = T(el.dataset.i18n); });
    document.querySelectorAll('[data-i18n-ph]').forEach(el => { el.placeholder = T(el.dataset.i18nPh); });
    document.querySelectorAll('.lang button').forEach(b => b.classList.toggle('on', b.dataset.lang === S.lang));
    $('btnScr').textContent = T('genScr');
    fillPitch(); fillHeadroom(); fillRig();
  }

  /* ---------- state <-> form ---------- */
  function writeForm() {
    NUM_FIELDS.forEach(k => { $(k).value = S[k]; });
    OPT_NUM_FIELDS.forEach(k => { $(k).value = S[k] > 0 ? S[k] : ''; });
    SEL_FIELDS.forEach(k => { if ($(k)) $(k).value = String(S[k]); });
    CHK_FIELDS.forEach(k => { $(k).checked = !!S[k]; });
    $('projectName').value = S.projectName;
    document.querySelectorAll('input[name=refresh]').forEach(r => { r.checked = Number(r.value) === Number(S.refresh); });
    const custom = S.cabPreset === 'custom';
    $('cabW').disabled = !custom; $('cabH').disabled = !custom;
    $('partialWrap').style.display = S.mixedRows ? '' : 'none';
    $('rigWrap').style.display = S.hung ? '' : 'none';
    $('customPxWrap').style.display = S.pitch === 'custom' ? '' : 'none';
  }

  function num(v, d) { const n = parseFloat(String(v).replace(',', '.')); return isFinite(n) ? n : d; }

  function readForm() {
    NUM_FIELDS.forEach(k => { S[k] = num($(k).value, S[k]); });
    OPT_NUM_FIELDS.forEach(k => { S[k] = Math.max(0, num($(k).value, 0)); });
    SEL_FIELDS.forEach(k => { if ($(k)) S[k] = $(k).value; });
    CHK_FIELDS.forEach(k => { S[k] = $(k).checked; });
    S.projectName = $('projectName').value;
    const r = document.querySelector('input[name=refresh]:checked'); if (r) S.refresh = Number(r.value);
    S.headroom = Number(S.headroom); S.portsPerSender = Number(S.portsPerSender);
    if (S.cabPreset !== 'custom') {
      const [w, h] = S.cabPreset.split('x').map(Number); S.cabW = w; S.cabH = h;
    }
    S.cabW = Math.max(0.1, S.cabW); S.cabH = Math.max(0.1, S.cabH);
    S.screenW = Math.max(S.cabW, S.screenW); S.screenH = Math.max(0.1, S.screenH);
    S.customPx = Math.max(8, Math.min(2048, Math.round(S.customPx)));
  }

  /* ---------- compute + render ---------- */
  function compute() {
    const pxW = S.pitch === 'custom' ? S.customPx : pxFor(S.pitch);
    const grid = Engine.buildGrid({
      screenW: S.screenW, screenH: S.screenH, cabW: S.cabW, cabH: S.cabH, pxW,
      cabKg: S.cabKg, cabA: S.cabA, mixedRows: S.mixedRows, partialRowPos: S.partialRowPos,
      partialKg: S.partialKg, partialA: S.partialA,
    });
    const cap = Engine.portCapacity(S.refresh, S.headroom);
    const options = Engine.portLayouts(grid, cap, 12);
    const tot = Engine.totals(grid, S.voltage);
    if (!options.length) return { grid, cap, options, tot, layout: null };
    if (S.optionIndex >= options.length) S.optionIndex = 0;
    const chainOpts = { orientation: S.orientation, pattern: S.pattern, startV: S.startV, startH: S.startH };
    const layout = Engine.buildLayout(grid, options[S.optionIndex], chainOpts, cap, { portLen: S.portLen, shortLen: S.shortLen });
    let rig = null;
    if (S.hung) {
      const points = S.rigPoints === 'auto' ? Engine.autoRigPoints(grid.widthM) : Number(S.rigPoints);
      rig = { points, reactions: Engine.beamReactions(points), kgTotal: tot.kg, fmt: v => fmt(v, 1) };
    }
    return { grid, cap, options, tot, layout, rig, option: options[S.optionIndex] };
  }

  function summaryRows(m) {
    const g = m.grid, L = m.layout, opt = m.option;
    const rows = [];
    const fullRow = g.rows.find(r => !r.partial) || g.rows[0];
    const pr = g.rows.find(r => r.partial);
    const nFull = g.cols * g.rows.filter(r => !r.partial).length, nPart = g.partialCount;
    const cabTxt = (n, h, px) => `${n} ${T('pcs')} ${fmtm(g.cabW)}×${fmtm(h)} m (${g.pxW}×${px} px)`;
    rows.push([`${g.cols} × ${g.rows.length} ${T('cabinets')}`, `${T('total')} ${g.cabinetCount}`]);
    if (pr) rows.push([T('sOfWhich'), `${cabTxt(nFull, fullRow.h, fullRow.px)} + ${cabTxt(nPart, pr.h, pr.px)}`]);
    const part = (n, v, d, unit) => `${fmt(n * v, d)} ${unit} (${n}×${fmt(v, d === 1 ? 1 : 2)} ${unit})`;
    rows.push([T('sTotalWeight'), pr
      ? `${part(nFull, fullRow.kg, 1, 'kg')} + ${part(nPart, pr.kg, 1, 'kg')} = ${fmt(m.tot.kg, 1)} kg`
      : `${g.cabinetCount} × ${fmt(fullRow.kg, 1)} kg = ${fmt(m.tot.kg, 1)} kg`]);
    rows.push([T('sTotalCurrent'), (pr
      ? `${part(nFull, fullRow.a, 2, 'A')} + ${part(nPart, pr.a, 2, 'A')} = ${fmt(m.tot.amps, 2)} A`
      : `${g.cabinetCount} × ${fmt(fullRow.a, 2)} A = ${fmt(m.tot.amps, 2)} A`) + ` · ${fmt(m.tot.watts)} W @ ${fmt(S.voltage)} V`]);
    rows.push([T('sCircuits'), `${m.tot.circuits16}`]);
    rows.push([T('sEffSize'), `${fmtm(g.widthM)} × ${fmtm(g.heightM)} m`]);
    rows.push([T('sResCab'), pr ? `${g.pxW} × ${fullRow.px} px + ${g.pxW} × ${pr.px} px` : `${g.pxW} × ${fullRow.px} px`]);
    rows.push([T('sResTotal'), `${g.widthPx} × ${g.heightPx} px`]);
    if (L) {
      const cabs = L.ports.map(p => p.cabinets);
      rows.push([T('sLayout'), T('sBlocks')
        .replace('{a}', Math.min.apply(null, cabs)).replace('{b}', Math.max.apply(null, cabs))
        .replace('{c}', fmt(opt.minLoad * 100, 1)).replace('{d}', fmt(opt.maxLoad * 100, 1))]);
      rows.push([T('sTotalPorts'), `${L.ports.length} @ ${S.refresh}Hz`]);
      rows.push([T('sSenders'), `${Math.ceil(L.ports.length / S.portsPerSender)} × ${S.portsPerSender} ${T('port').toLowerCase()}`]);
      rows.push([T('sCables'), cableText(L)]);
    }
    if (m.rig) {
      rows.push([T('sRig'), m.rig.reactions.map(r => `${fmt(r * m.tot.kg, 1)} kg`).join(' · ')]);
    }
    return rows;
  }

  const LINK_LABEL = { short: 'sShort', longer: 'sLonger', return: 'sReturn' };
  function cableText(L) {
    const parts = [`${L.cables.port.count} × ${fmt(L.cables.port.length, 1)}m (${T('sPortCable')})`];
    L.cables.links.forEach(c => parts.push(`${c.count} × ${fmt(c.length, 1)}m (${T(LINK_LABEL[c.type])})`));
    return parts.join(' + ');
  }

  function render() {
    readForm(); save();
    const m = compute(); current = m;
    const g = m.grid, L = m.layout;

    const tiles = [
      [T('dimension'), `${fmtm(g.widthM)} × ${fmtm(g.heightM)} m`],
      [T('weight'), `${fmt(m.tot.kg, 1)} kg`],
      [T('resolution'), `${g.widthPx} × ${g.heightPx} px`],
      [T('totalPixels'), `${fmt(g.widthPx * g.heightPx)} px`],
      [T('nPorts'), L ? `${L.ports.length} @ ${S.refresh}Hz` : '—'],
      [T('consumption'), `${fmt(m.tot.amps, 1)} A (${fmt(m.tot.watts)} W)`],
    ];
    $('tiles').innerHTML = tiles.map(([k, v]) => `<div class="tile"><div class="k">${k}</div><div class="v">${v}</div></div>`).join('');

    if (!L) {
      const maxCab = Math.max.apply(null, g.rows.map(r => r.px)) * g.pxW;
      $('summary').innerHTML = `<span class="warn">${T('noLayout').replace('{cab}', fmt(maxCab)).replace('{cap}', fmt(m.cap))}</span>`;
      $('legend').innerHTML = '';
      const c = $('diagram'); c.width = 1; c.height = 1;
      $('optBtn').textContent = `${T('option')} 0/0`;
      return;
    }
    $('summary').innerHTML = summaryRows(m).map(([k, v]) => `${k}: <b>${v}</b>`).join('<br>');
    $('legend').innerHTML = L.ports.map(p => {
      const pct = Math.round(p.load * 100);
      const cls = p.load > 0.95 ? '#ff5d5d' : p.load > 0.85 ? '#f2803a' : p.load > 0.6 ? '#e2b93b' : '#46c45a';
      return `<li><i style="background:${Diagram.portColor(p.index)}"></i>${T('port')} ${p.index + 1} (${p.cabinets} ${T('cab')}, ${fmt(p.pixels)}px, <span class="pct" style="color:${cls}">${pct}%</span>)</li>`;
    }).join('');
    $('optBtn').textContent = `${T('option')} ${S.optionIndex + 1}/${m.options.length}`;
    $('tglOrient').classList.toggle('on', S.orientation === 'h');
    $('tglStart').classList.toggle('on', S.startV === 'bottom');
    drawDiagram();
  }

  function drawDiagram() {
    if (!current || !current.layout) return;
    const c = $('diagram');
    const dpr = Math.min(3, window.devicePixelRatio || 1);
    const cssW = c.parentElement.clientWidth - 16;
    const W = Math.max(280, Math.round(cssW * dpr));
    const model = { grid: current.grid, layout: current.layout, rig: current.rig };
    const geo = Diagram.geometry(model, W);
    c.width = W; c.height = Math.round(geo.height);
    Diagram.draw(c.getContext('2d'), model, W, { bw: S.printer });
  }

  /* ---------- exports ---------- */
  function baseName() {
    const m = current;
    return S.projectName.trim() || `Screen-${fmtNum(m.grid.widthM)}x${fmtNum(m.grid.heightM)}m-${m.grid.pxW}px`;
  }
  function fmtNum(v) { return String(Math.round(v * 100) / 100); }

  function done(p) {
    const share = window.AndroidBridge && window.AndroidBridge.shareLast
      ? { label: T('share'), run: () => window.AndroidBridge.shareLast() } : null;
    p.then(loc => toast(T('saved') + loc, share)).catch(e => toast('⚠ ' + (e && e.message ? e.message : e)));
  }

  function doScr() {
    if (!current || !current.layout) return;
    const bytes = ScrWriter.build(current.grid, current.layout, { portsPerSender: S.portsPerSender, name: baseName().slice(0, 20) });
    done(Exporter.saveBytes(`${Exporter.safeName(baseName())}.scr`, bytes, 'application/octet-stream'));
  }

  function doJpg() {
    if (!current || !current.layout) return;
    const g = current.grid;
    const head = `${baseName()} — ${fmtm(g.widthM)} × ${fmtm(g.heightM)} m · ${g.widthPx} × ${g.heightPx} px · ${current.layout.ports.length} ${T('port').toLowerCase()} @ ${S.refresh}Hz`;
    done(Exporter.exportJpg({ grid: g, layout: current.layout, rig: current.rig }, baseName(), S.printer, head));
  }

  function doPdf() {
    if (!current || !current.layout) return;
    const m = current, g = m.grid, L = m.layout;
    const now = new Date();
    const report = {
      title: `${T('reportTitle')} — ${baseName()}`,
      subtitle: `${fmtm(g.widthM)} × ${fmtm(g.heightM)} m · ${g.widthPx} × ${g.heightPx} px · ${L.ports.length} ${T('port').toLowerCase()} @ ${S.refresh}Hz · ${T('option')} ${S.optionIndex + 1}/${m.options.length}`,
      generated: `${T('generated')}: ${now.toLocaleString(locale())}`,
      headings: { summary: T('summary'), ports: T('portList'), cables: T('cableList'), settings: T('settings') },
      summary: summaryRows(m).filter(r => r[0] !== T('sCables')),
      ports: [[T('port'), T('cab'), 'px', '%']].concat(L.ports.map(p => [`${T('port')} ${p.index + 1}`, p.cabinets, fmt(p.pixels), `${Math.round(p.load * 100)}%`])),
      cables: [[`${T('sPortCable')} (${fmt(L.cables.port.length, 1)} m)`, `${L.cables.port.count}`]]
        .concat(L.cables.links.map(c => [`${T(LINK_LABEL[c.type])} (${fmt(c.length, 1)} m)`, `${c.count}`])),
      settings: [
        [T('cabPreset'), `${fmtm(S.cabW)} × ${fmtm(S.cabH)} m`],
        [T('cabKg'), fmt(S.cabKg, 2)], [T('cabA'), fmt(S.cabA, 3)],
        ...(g.partialCount ? [[T('partialKg'), fmt(g.rows.find(r => r.partial).kg, 2)], [T('partialA'), fmt(g.rows.find(r => r.partial).a, 3)]] : []),
        [T('resPreset'), S.pitch === 'custom' ? `${S.customPx} px` : `${S.pitch} · ${g.pxW} px`],
        [T('refresh'), `${S.refresh} Hz`], [T('headroom'), `${S.headroom}% (${fmt(m.cap)} ${T('pxPerPort')})`],
        [T('portsPerSender'), String(S.portsPerSender)],
        [T('orientation'), S.orientation === 'h' ? T('horizontal') : T('vertical')],
        [T('pattern'), S.pattern === 'snake' ? T('snake') : T('sequential')],
        [T('startV'), `${S.startV === 'bottom' ? T('posBottom') : T('posTop')} / ${S.startH === 'left' ? T('left') : T('right')}`],
      ],
      footer: `LED Mapper Configurator v${VERSION}`,
    };
    done(Exporter.exportPdf({ grid: g, layout: L, rig: m.rig }, baseName(), S.printer, report));
  }

  function doSave() {
    const data = Object.assign({ app: 'led-mapper-configurator', version: VERSION }, S);
    const bytes = new TextEncoder().encode(JSON.stringify(data, null, 2));
    done(Exporter.saveBytes(`${Exporter.safeName(baseName())}.json`, bytes, 'application/json'));
  }

  function doLoad(file) {
    const rd = new FileReader();
    rd.onload = () => {
      try {
        const d = JSON.parse(rd.result);
        if (typeof d !== 'object' || d === null || !('screenW' in d)) throw new Error('bad');
        const lang = S.lang;
        Object.keys(DEFAULTS).forEach(k => { if (k in d) S[k] = d[k]; });
        S.lang = lang;
        applyLang(); writeForm(); render(); toast(T('loaded'));
      } catch (e) { toast(T('errLoad')); }
    };
    rd.readAsText(file);
  }

  /* ---------- events ---------- */
  function stepSize(k) {
    if (k === 'screenW') return S.cabW;
    if (k === 'screenH') return S.mixedRows ? Math.min(0.5, S.cabH) : S.cabH;
    if (k === 'cabKg') return 0.1;
    if (k === 'cabA') return 0.05;
    return 1;
  }

  function bind() {
    document.querySelectorAll('[data-step]').forEach(b => b.addEventListener('click', () => {
      const [k, dir] = b.dataset.step.split(':');
      const st = stepSize(k);
      let v = num($(k).value, S[k]) + (dir === '+' ? st : -st);
      v = Math.max(k.startsWith('screen') ? st : 0, Math.round(v * 1000) / 1000);
      $(k).value = v; S.optionIndex = 0; render();
    }));
    document.querySelectorAll('input, select').forEach(el => {
      if (el.id === 'fileLoad' || el.id === 'printer') return;
      el.addEventListener('change', () => {
        readForm(); S.optionIndex = el.id === 'orientation' || el.id === 'pattern' || el.id === 'startV' || el.id === 'startH' || el.id === 'projectName' ? S.optionIndex : 0;
        if (el.id === 'cabPreset' && CAB_DEFAULTS[S.cabPreset]) { Object.assign(S, CAB_DEFAULTS[S.cabPreset]); }
        if (el.id === 'cabPreset' || el.id === 'cabW') fillPitch();
        if (el.name === 'refresh') fillHeadroom();
        if (el.id === 'hung' && S.hung) { S.startV = 'top'; }
        writeForm(); render();
      });
    });
    $('printer').addEventListener('change', () => { S.printer = $('printer').checked; save(); drawDiagram(); });
    document.querySelectorAll('.lang button').forEach(b => b.addEventListener('click', () => {
      S.lang = b.dataset.lang; applyLang(); writeForm(); render();
    }));
    $('optBtn').addEventListener('click', () => {
      if (!current || !current.options.length) return;
      S.optionIndex = (S.optionIndex + 1) % current.options.length; render();
    });
    $('tglOrient').addEventListener('click', () => { S.orientation = S.orientation === 'h' ? 'v' : 'h'; writeForm(); render(); });
    $('tglStart').addEventListener('click', () => { S.startV = S.startV === 'bottom' ? 'top' : 'bottom'; writeForm(); render(); });
    $('btnScr').addEventListener('click', doScr);
    $('btnJpg').addEventListener('click', doJpg);
    $('btnPdf').addEventListener('click', doPdf);
    $('btnSave').addEventListener('click', doSave);
    $('btnLoad').addEventListener('click', () => $('fileLoad').click());
    $('fileLoad').addEventListener('change', e => { const f = e.target.files[0]; if (f) doLoad(f); e.target.value = ''; });
    let rt; window.addEventListener('resize', () => { clearTimeout(rt); rt = setTimeout(drawDiagram, 120); });
  }

  $('ver').textContent = 'v' + VERSION;
  applyLang(); writeForm(); bind(); render();
})();
