/*
 * LED Mapper Configurator — calculation engine.
 * Pure functions, no DOM. Works in the browser (window.Engine) and in Node (module.exports).
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.Engine = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  /**
   * Pixel pitch presets (mm), grouped by use. Horizontal px per cabinet = round(cabinetWidthMm / pitch).
   * Nominal names follow common market labels (e.g. P3.9 = 3.91 mm, P1.9 = 1.953 mm).
   */
  const PITCH_PRESETS = [
    { id: 'P0.9', pitch: 0.9375, group: 'fine' },
    { id: 'P1.2', pitch: 1.25, group: 'fine' },
    { id: 'P1.5', pitch: 1.5625, group: 'fine' },
    { id: 'P1.8', pitch: 1.875, group: 'fine' },
    { id: 'P1.9', pitch: 1.953, group: 'fine' },
    { id: 'P2.0', pitch: 2.0, group: 'indoor' },
    { id: 'P2.5', pitch: 2.5, group: 'indoor' },
    { id: 'P2.6', pitch: 2.604, group: 'indoor' },
    { id: 'P2.8', pitch: 2.841, group: 'indoor' },
    { id: 'P2.9', pitch: 2.976, group: 'indoor' },
    { id: 'P3.9', pitch: 3.906, group: 'indoor' },
    { id: 'P4.8', pitch: 4.808, group: 'outdoor' },
    { id: 'P5.9', pitch: 5.952, group: 'outdoor' },
    { id: 'P6.6', pitch: 6.667, group: 'outdoor' },
    { id: 'P8', pitch: 8.0, group: 'outdoor' },
    { id: 'P10', pitch: 10.0, group: 'outdoor' },
  ];

  /** Gigabit port capacity: 936,000,000 / (refresh × 24). */
  function portCapacity(refreshHz, headroomPct) {
    const raw = Math.floor(936000000 / (refreshHz * 24));
    return Math.floor(raw * (1 - (headroomPct || 0) / 100));
  }

  function round2(v) { return Math.round(v * 100) / 100; }

  /**
   * Build the cabinet grid.
   * p = { screenW, screenH, cabW, cabH, pxW, cabKg, cabA, mixedRows, partialRowPos: 'bottom'|'top' }
   */
  function buildGrid(p) {
    const eps = 1e-6;
    const cols = Math.max(1, Math.round(p.screenW / p.cabW + eps));
    const pxFull = Math.max(1, Math.round(p.pxW * p.cabH / p.cabW));
    const rows = [];
    const full = { h: p.cabH, px: pxFull, kg: p.cabKg, a: p.cabA, partial: false };
    let nFull, partial = null;
    if (p.mixedRows) {
      nFull = Math.floor(p.screenH / p.cabH + eps);
      const rem = round2(p.screenH - nFull * p.cabH);
      if (rem >= 0.05) {
        const f = rem / p.cabH;
        partial = {
          h: rem,
          px: Math.max(1, Math.round(p.pxW * rem / p.cabW)),
          kg: round2(p.cabKg * f),
          a: Math.round(p.cabA * f * 1000) / 1000,
          partial: true,
        };
      }
      if (nFull === 0 && !partial) nFull = 1;
    } else {
      nFull = Math.max(1, Math.round(p.screenH / p.cabH + eps));
    }
    for (let i = 0; i < nFull; i++) rows.push(Object.assign({}, full));
    if (partial) {
      if (p.partialRowPos === 'top') rows.unshift(partial);
      else rows.push(partial);
    }
    // pixel / metre offsets
    let y = 0, ym = 0;
    rows.forEach(r => { r.y = y; r.ym = ym; y += r.px; ym += r.h; });
    return {
      cols, rows, pxW: p.pxW, cabW: p.cabW,
      widthPx: cols * p.pxW, heightPx: y,
      widthM: round2(cols * p.cabW), heightM: round2(ym),
      cabinetCount: cols * rows.length,
      partialCount: partial ? cols : 0,
    };
  }

  function regionPixels(grid, c0, c1, r0, r1) {
    let h = 0;
    for (let r = r0; r < r1; r++) h += grid.rows[r].px;
    return (c1 - c0) * grid.pxW * h;
  }

  /** Greedy split of a rectangle into full-height column strips (or full-width row strips). */
  function stripSplit(grid, reg, cap, axis) {
    const out = [];
    if (axis === 'cols') {
      let x = reg.c0;
      while (x < reg.c1) {
        if (regionPixels(grid, x, x + 1, reg.r0, reg.r1) > cap) return null;
        let w = 1;
        while (x + w < reg.c1 && regionPixels(grid, x, x + w + 1, reg.r0, reg.r1) <= cap) w++;
        out.push({ c0: x, c1: x + w, r0: reg.r0, r1: reg.r1 });
        x += w;
      }
    } else {
      let y = reg.r0;
      while (y < reg.r1) {
        if (regionPixels(grid, reg.c0, reg.c1, y, y + 1) > cap) return null;
        let h = 1;
        while (y + h < reg.r1 && regionPixels(grid, reg.c0, reg.c1, y, y + h + 1) <= cap) h++;
        out.push({ c0: reg.c0, c1: reg.c1, r0: y, r1: y + h });
        y += h;
      }
    }
    return out;
  }

  /** Split any rectangle into pieces that each fit under cap. */
  function splitRegion(grid, reg, cap) {
    if (reg.c1 <= reg.c0 || reg.r1 <= reg.r0) return [];
    if (regionPixels(grid, reg.c0, reg.c1, reg.r0, reg.r1) <= cap) return [reg];
    const a = stripSplit(grid, reg, cap, 'cols');
    const b = stripSplit(grid, reg, cap, 'rows');
    if (a && b) return a.length <= b.length ? a : b;
    if (a) return a;
    if (b) return b;
    // Neither a single column nor a single row fits — halve the larger side.
    const w = reg.c1 - reg.c0, h = reg.r1 - reg.r0;
    if (w === 1 && h === 1) return null; // one cabinet exceeds a port
    if (w >= h) {
      const m = reg.c0 + Math.ceil(w / 2);
      const l = splitRegion(grid, { c0: reg.c0, c1: m, r0: reg.r0, r1: reg.r1 }, cap);
      const r = splitRegion(grid, { c0: m, c1: reg.c1, r0: reg.r0, r1: reg.r1 }, cap);
      return l && r ? l.concat(r) : null;
    }
    const m = reg.r0 + Math.ceil(h / 2);
    const t = splitRegion(grid, { c0: reg.c0, c1: reg.c1, r0: reg.r0, r1: m }, cap);
    const bt = splitRegion(grid, { c0: reg.c0, c1: reg.c1, r0: m, r1: reg.r1 }, cap);
    return t && bt ? t.concat(bt) : null;
  }

  function layoutSignature(regs) {
    return regs.map(r => `${r.c0},${r.c1},${r.r0},${r.r1}`).sort().join('|');
  }

  /**
   * Generate port-layout options: tile the screen with the largest uniform cabinet block that
   * fits under the port capacity, then fill the leftover strips.
   */
  function portLayouts(grid, cap, maxOptions) {
    maxOptions = maxOptions || 12;
    const C = grid.cols, R = grid.rows.length;
    const seen = new Set();
    const results = [];

    const tryLayout = (regs, bw, bh) => {
      if (!regs || !regs.length) return;
      const sig = layoutSignature(regs);
      if (seen.has(sig)) return;
      seen.add(sig);
      const loads = regs.map(r => regionPixels(grid, r.c0, r.c1, r.r0, r.r1) / cap);
      results.push({
        regions: regs, block: { w: bw, h: bh },
        ports: regs.length,
        minLoad: Math.min.apply(null, loads), maxLoad: Math.max.apply(null, loads),
      });
    };

    for (let bw = 1; bw <= C; bw++) {
      for (let bh = 1; bh <= R; bh++) {
        const nx = Math.floor(C / bw), ny = Math.floor(R / bh);
        // Only "maximal" blocks: growing either side must break the cap (or the grid).
        let ok = true;
        const main = [];
        for (let i = 0; i < nx && ok; i++) {
          for (let j = 0; j < ny; j++) {
            const reg = { c0: i * bw, c1: (i + 1) * bw, r0: j * bh, r1: (j + 1) * bh };
            if (regionPixels(grid, reg.c0, reg.c1, reg.r0, reg.r1) > cap) { ok = false; break; }
            main.push(reg);
          }
        }
        if (!ok) continue;
        const growW = bw < C && regionPixels(grid, 0, bw + 1, 0, bh) <= cap;
        const growH = bh < R && regionPixels(grid, 0, bw, 0, bh + 1) <= cap;
        if (growW && growH) continue;
        const mw = nx * bw, mh = ny * bh;
        // Variant A: right strip full height, bottom strip under the main block.
        const ra = splitRegion(grid, { c0: mw, c1: C, r0: 0, r1: R }, cap);
        const ba = splitRegion(grid, { c0: 0, c1: mw, r0: mh, r1: R }, cap);
        if (ra && ba) tryLayout(main.concat(ra, ba), bw, bh);
        // Variant B: bottom strip full width, right strip beside the main block.
        const rb = splitRegion(grid, { c0: mw, c1: C, r0: 0, r1: mh }, cap);
        const bb = splitRegion(grid, { c0: 0, c1: C, r0: mh, r1: R }, cap);
        if (rb && bb) tryLayout(main.concat(rb, bb), bw, bh);
      }
    }
    // Pure strip layouts as extra variants.
    const sc = stripSplit(grid, { c0: 0, c1: C, r0: 0, r1: R }, cap, 'cols');
    if (sc) tryLayout(sc, 0, 0);
    const sr = stripSplit(grid, { c0: 0, c1: C, r0: 0, r1: R }, cap, 'rows');
    if (sr) tryLayout(sr, 0, 0);

    if (!results.length) return [];
    const minPorts = Math.min.apply(null, results.map(r => r.ports));
    const good = results.filter(r => r.ports <= minPorts + 1);
    good.sort((a, b) =>
      a.ports - b.ports ||
      (b.minLoad - a.minLoad) ||           // most balanced first
      (a.maxLoad - b.maxLoad));
    return good.slice(0, maxOptions).map(opt => {
      // Number ports column-major: left→right, then top→bottom.
      const regs = opt.regions.slice().sort((a, b) => a.c0 - b.c0 || a.r0 - b.r0);
      return Object.assign({}, opt, { regions: regs });
    });
  }

  /**
   * Cabinet order inside one port region.
   * opts = { orientation: 'h'|'v', pattern: 'snake'|'seq', startV: 'bottom'|'top', startH: 'left'|'right' }
   */
  function chainOrder(reg, opts) {
    const cols = [], rows = [];
    for (let c = reg.c0; c < reg.c1; c++) cols.push(c);
    for (let r = reg.r0; r < reg.r1; r++) rows.push(r);
    if (opts.startH === 'right') cols.reverse();
    if (opts.startV === 'bottom') rows.reverse();
    const out = [];
    if (opts.orientation === 'v') {
      cols.forEach((c, k) => {
        const rr = (opts.pattern === 'snake' && k % 2) ? rows.slice().reverse() : rows;
        rr.forEach(r => out.push({ c, r }));
      });
    } else {
      rows.forEach((r, k) => {
        const cc = (opts.pattern === 'snake' && k % 2) ? cols.slice().reverse() : cols;
        cc.forEach(c => out.push({ c, r }));
      });
    }
    return out;
  }

  const STD_LENGTHS = [0.5, 0.7, 1, 1.5, 2, 3, 5, 7, 10, 15, 20, 30];
  function stdLength(m) {
    for (const l of STD_LENGTHS) if (l + 1e-9 >= m) return l;
    return Math.ceil(m / 5) * 5;
  }

  /** Full layout: ports, chains, per-cabinet assignments, cables. */
  function buildLayout(grid, option, opts, cap, cable) {
    const cells = []; // [r][c] = { port, index }
    for (let r = 0; r < grid.rows.length; r++) cells.push(new Array(grid.cols).fill(null));
    const ports = [];
    let shortLinks = 0;
    const longLinks = {};
    option.regions.forEach((reg, pi) => {
      const chain = chainOrder(reg, opts);
      chain.forEach((p, i) => { cells[p.r][p.c] = { port: pi, index: i }; });
      for (let i = 1; i < chain.length; i++) {
        const a = chain[i - 1], b = chain[i];
        const dc = Math.abs(a.c - b.c), dr = Math.abs(a.r - b.r);
        if (dc + dr === 1) shortLinks++;
        else {
          // distance in metres between cabinet centres
          const ya = grid.rows[a.r].ym + grid.rows[a.r].h / 2;
          const yb = grid.rows[b.r].ym + grid.rows[b.r].h / 2;
          const d = dc * grid.cabW + Math.abs(ya - yb);
          const L = stdLength(d + 0.3);
          longLinks[L] = (longLinks[L] || 0) + 1;
        }
      }
      const px = regionPixels(grid, reg.c0, reg.c1, reg.r0, reg.r1);
      ports.push({
        index: pi, region: reg, chain, pixels: px, load: px / cap,
        cabinets: chain.length,
      });
    });
    return {
      ports, cells,
      cables: {
        port: { count: ports.length, length: cable.portLen },
        short: { count: shortLinks, length: cable.shortLen },
        long: Object.keys(longLinks).map(Number).sort((a, b) => a - b)
          .map(L => ({ length: L, count: longLinks[L] })),
      },
    };
  }

  /**
   * Continuous beam with n equal spans and uniform load (three-moment equation).
   * Returns support reaction fractions (sum = 1).
   */
  function beamReactions(points) {
    const n = points - 1; // spans
    if (n <= 0) return [1];
    if (n === 1) return [0.5, 0.5];
    // unknown moments M1..M(n-1); M0 = Mn = 0. w = 1, L = 1.
    // M(i-1) + 4 M(i) + M(i+1) = -1/2
    const m = n - 1;
    const a = new Array(m).fill(1), b = new Array(m).fill(4), c = new Array(m).fill(1), d = new Array(m).fill(-0.5);
    for (let i = 1; i < m; i++) { const k = a[i] / b[i - 1]; b[i] -= k * c[i - 1]; d[i] -= k * d[i - 1]; }
    const M = new Array(m);
    M[m - 1] = d[m - 1] / b[m - 1];
    for (let i = m - 2; i >= 0; i--) M[i] = (d[i] - c[i] * M[i + 1]) / b[i];
    const Mfull = [0].concat(M, [0]);
    // Shear at ends of span k (between supports k and k+1), w=1, L=1, sagging-positive moments:
    // left end V = 1/2 + (M(k+1) - M(k)), right end V = 1/2 - (M(k+1) - M(k))
    const R = new Array(n + 1).fill(0);
    for (let k = 0; k < n; k++) {
      const dM = Mfull[k + 1] - Mfull[k];
      R[k] += 0.5 + dM;
      R[k + 1] += 0.5 - dM;
    }
    const tot = R.reduce((s, v) => s + v, 0);
    return R.map(v => v / tot);
  }

  function autoRigPoints(widthM) {
    return Math.max(2, Math.min(12, Math.ceil(widthM / 3.5) + 1));
  }

  /** Totals (weight, current, power). */
  function totals(grid, voltage) {
    let kg = 0, a = 0;
    grid.rows.forEach(r => { kg += r.kg * grid.cols; a += r.a * grid.cols; });
    return {
      kg: round2(kg), amps: round2(a), watts: Math.round(a * voltage),
      circuits16: Math.max(1, Math.ceil(a / 12.8)), // 16 A breaker at 80 % continuous load
    };
  }

  return {
    PITCH_PRESETS, portCapacity, buildGrid, regionPixels, splitRegion, portLayouts,
    chainOrder, buildLayout, beamReactions, autoRigPoints, totals, stdLength,
  };
});
