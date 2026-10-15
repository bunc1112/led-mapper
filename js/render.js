/* Wiring diagram renderer (Canvas 2D). Used for the on-screen view, JPG and PDF export. */
(function () {
  'use strict';

  const PALETTE = [
    '#e5484d', '#e2b93b', '#46c45a', '#2fd5a0', '#3b9eff', '#8b5cf6', '#e14fd4', '#f2803a',
    '#22c3d6', '#a3d94a', '#ff6b9a', '#6c7cff', '#c9a227', '#3fbf8f', '#b06cf2', '#ff9157',
  ];
  const GRAYS = ['#ffffff', '#d9d9d9', '#f0f0f0', '#bfbfbf', '#e6e6e6', '#cccccc'];

  function portColor(i) {
    if (i < PALETTE.length) return PALETTE[i];
    const h = (i * 137.508) % 360;
    return `hsl(${h.toFixed(0)},70%,58%)`;
  }

  function loadColor(load, bw) {
    if (bw) return '#000';
    if (load > 0.95) return '#ff5d5d';
    if (load > 0.85) return '#f2803a';
    if (load > 0.6) return '#e2b93b';
    return '#46c45a';
  }

  function arrow(ctx, x1, y1, x2, y2, size) {
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
    // arrow head at midpoint
    const mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
    const a = Math.atan2(y2 - y1, x2 - x1);
    ctx.beginPath();
    ctx.moveTo(mx + Math.cos(a) * size, my + Math.sin(a) * size);
    ctx.lineTo(mx + Math.cos(a + 2.5) * size, my + Math.sin(a + 2.5) * size);
    ctx.lineTo(mx + Math.cos(a - 2.5) * size, my + Math.sin(a - 2.5) * size);
    ctx.closePath(); ctx.fill();
  }

  /**
   * Compute geometry for a given drawing width.
   * model = { grid, layout, rig: {points, reactions, kgTotal} | null }
   */
  function geometry(model, width) {
    const g = model.grid;
    const pad = Math.max(10, width * 0.03);
    const rigH = model.rig ? Math.max(42, width * 0.06) : 0;
    const inner = width - pad * 2;
    const ppm = inner / g.widthM; // px per metre
    const heightM = g.heightM;
    const height = pad * 2 + rigH + heightM * ppm;
    return { pad, rigH, ppm, width, height, x0: pad, y0: pad + rigH };
  }

  function draw(ctx, model, width, opts) {
    opts = opts || {};
    const bw = !!opts.bw;
    const g = model.grid, L = model.layout;
    const geo = geometry(model, width);
    const { ppm, x0, y0 } = geo;
    const cw = g.cabW * ppm;
    const font = opts.font || 'system-ui, -apple-system, Roboto, sans-serif';

    // background
    ctx.fillStyle = bw ? '#ffffff' : (opts.bg || '#1b1f27');
    ctx.fillRect(0, 0, geo.width, geo.height);

    const rowY = r => y0 + g.rows[r].ym * ppm;
    const rowH = r => g.rows[r].h * ppm;
    const cx = c => x0 + c * cw + cw / 2;
    const cy = r => rowY(r) + rowH(r) / 2;

    // screen frame
    ctx.lineWidth = Math.max(2, cw * 0.05);
    ctx.strokeStyle = bw ? '#000' : '#e8ecf2';
    ctx.strokeRect(x0 - ctx.lineWidth, y0 - ctx.lineWidth,
      g.widthM * ppm + ctx.lineWidth * 2, g.heightM * ppm + ctx.lineWidth * 2);

    // cabinets
    const gap = Math.max(1, cw * 0.04);
    const labelSize = Math.max(7, Math.min(cw * 0.17, rowH(0) * 0.2, 16));
    for (let r = 0; r < g.rows.length; r++) {
      for (let c = 0; c < g.cols; c++) {
        const cell = L.cells[r][c];
        const x = x0 + c * cw + gap / 2, y = rowY(r) + gap / 2;
        const w = cw - gap, h = rowH(r) - gap;
        const col = portColor(cell.port);
        if (bw) {
          ctx.fillStyle = GRAYS[cell.port % GRAYS.length];
          ctx.fillRect(x, y, w, h);
          ctx.strokeStyle = '#000'; ctx.lineWidth = 1;
          ctx.strokeRect(x, y, w, h);
        } else {
          ctx.globalAlpha = 0.42; ctx.fillStyle = col; ctx.fillRect(x, y, w, h);
          ctx.globalAlpha = 1; ctx.strokeStyle = col; ctx.lineWidth = Math.max(1, cw * 0.025);
          ctx.strokeRect(x, y, w, h);
        }
        ctx.fillStyle = bw ? '#000' : '#f3f5f8';
        ctx.font = `${labelSize}px ${font}`;
        ctx.textAlign = 'left'; ctx.textBaseline = 'top';
        ctx.fillText(`${cell.port + 1}.${cell.index + 1}`, x + labelSize * 0.3, y + labelSize * 0.25);
      }
    }

    // port region outlines
    L.ports.forEach(p => {
      const reg = p.region;
      ctx.strokeStyle = bw ? '#000' : portColor(p.index);
      ctx.lineWidth = Math.max(2, cw * (bw ? 0.07 : 0.05));
      const top = rowY(reg.r0), bottom = rowY(reg.r1 - 1) + rowH(reg.r1 - 1);
      ctx.strokeRect(x0 + reg.c0 * cw, top, (reg.c1 - reg.c0) * cw, bottom - top);
    });

    // chain arrows
    const aSize = Math.max(3, Math.min(cw, rowH(0)) * 0.1);
    L.ports.forEach(p => {
      ctx.strokeStyle = bw ? '#000' : '#ffffff';
      ctx.fillStyle = ctx.strokeStyle;
      ctx.lineWidth = Math.max(1, cw * 0.02);
      for (let i = 1; i < p.chain.length; i++) {
        const a = p.chain[i - 1], b = p.chain[i];
        const long = Math.abs(a.c - b.c) + Math.abs(a.r - b.r) > 1;
        if (long) ctx.setLineDash([aSize, aSize * 0.8]);
        arrow(ctx, cx(a.c), cy(a.r), cx(b.c), cy(b.r), aSize);
        if (long) ctx.setLineDash([]);
      }
    });

    // port start badges with load ring
    L.ports.forEach(p => {
      const s = p.chain[0];
      const R = Math.max(7, Math.min(cw, rowH(s.r)) * 0.27);
      const x = cx(s.c), y = cy(s.r);
      ctx.fillStyle = bw ? '#fff' : '#11141a';
      ctx.beginPath(); ctx.arc(x, y, R, 0, Math.PI * 2); ctx.fill();
      ctx.lineWidth = Math.max(2, R * 0.22);
      ctx.strokeStyle = bw ? '#bbb' : 'rgba(255,255,255,0.18)';
      ctx.beginPath(); ctx.arc(x, y, R, 0, Math.PI * 2); ctx.stroke();
      ctx.strokeStyle = loadColor(p.load, bw);
      ctx.beginPath(); ctx.arc(x, y, R, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Math.min(1, p.load)); ctx.stroke();
      ctx.fillStyle = bw ? '#000' : '#fff';
      ctx.font = `bold ${R * 1.05}px ${font}`;
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(String(p.index + 1), x, y + R * 0.05);
    });

    // rigging beam
    if (model.rig) {
      const n = model.rig.points;
      const by = geo.pad + geo.rigH * 0.62;
      const bx0 = x0, bx1 = x0 + g.widthM * ppm;
      const line = bw ? '#000' : '#3b9eff';
      ctx.strokeStyle = line; ctx.fillStyle = line; ctx.lineWidth = Math.max(2, geo.rigH * 0.06);
      ctx.beginPath(); ctx.moveTo(bx0, by); ctx.lineTo(bx1, by); ctx.stroke();
      const fs = Math.max(8, geo.rigH * 0.22);
      for (let i = 0; i < n; i++) {
        const x = bx0 + (bx1 - bx0) * (n === 1 ? 0.5 : i / (n - 1));
        ctx.lineWidth = Math.max(1.5, geo.rigH * 0.04);
        ctx.beginPath(); ctx.moveTo(x, by); ctx.lineTo(x, y0); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(x, by + 1); ctx.lineTo(x - fs * 0.4, by - fs * 0.5); ctx.lineTo(x + fs * 0.4, by - fs * 0.5); ctx.closePath(); ctx.fill();
        const pct = model.rig.reactions[i];
        ctx.fillStyle = bw ? '#000' : '#cfd6e2';
        ctx.font = `${fs}px ${font}`; ctx.textBaseline = 'bottom';
        ctx.textAlign = i === 0 ? 'left' : (i === n - 1 ? 'right' : 'center');
        const kgTxt = model.rig.fmt ? model.rig.fmt(pct * model.rig.kgTotal) : (pct * model.rig.kgTotal).toFixed(1);
        ctx.fillText(`${Math.round(pct * 100)}% · ${kgTxt} kg`, x, by - fs * 0.6);
        ctx.fillStyle = line;
      }
    }
    return geo;
  }

  window.Diagram = { draw, geometry, portColor };
})();
