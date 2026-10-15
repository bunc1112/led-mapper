/* File output: Android bridge when running in the app, browser download otherwise. */
(function () {
  'use strict';

  function bytesToBase64(bytes) {
    let bin = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      bin += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }
    return btoa(bin);
  }

  function safeName(s) {
    return (s || 'Screen').trim().replace(/[\\/:*?"<>|]+/g, '_').replace(/\s+/g, '-').slice(0, 60) || 'Screen';
  }

  const iosBridge = () => window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ledmapper;
  const isIOSBrowser = () => /iP(hone|ad|od)/.test(navigator.userAgent) ||
    (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);

  /** Save bytes. Returns a promise resolving to a human-readable location. */
  function saveBytes(filename, bytes, mime) {
    // Android app: native bridge writes to Downloads/LED Mapper.
    if (window.AndroidBridge && window.AndroidBridge.saveFile) {
      const res = window.AndroidBridge.saveFile(filename, bytesToBase64(bytes), mime);
      if (res && res.indexOf('ERROR') === 0) return Promise.reject(new Error(res));
      return Promise.resolve(res || filename);
    }
    // iOS app: native bridge writes to Files › LED Mapper and opens the share sheet.
    if (iosBridge()) {
      return iosBridge().postMessage({ action: 'save', name: filename, data: bytesToBase64(bytes), mime })
        .then(res => {
          if (res && String(res).indexOf('ERROR') === 0) throw new Error(res);
          return res || filename;
        });
    }
    const blob = new Blob([bytes], { type: mime });
    // iPhone/iPad Safari or home-screen web app: use the system share sheet when possible.
    if (isIOSBrowser() && navigator.canShare) {
      try {
        const file = new File([blob], filename, { type: mime });
        if (navigator.canShare({ files: [file] })) {
          return navigator.share({ files: [file] }).then(() => filename).catch(err => {
            if (err && err.name === 'AbortError') return filename;
            return downloadBlob(blob, filename);
          });
        }
      } catch (e) { /* fall through to download */ }
    }
    return downloadBlob(blob, filename);
  }

  function downloadBlob(blob, filename) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = filename;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    return Promise.resolve(filename);
  }

  function dataUrlToBytes(url) {
    const b64 = url.split(',')[1];
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  function renderCanvas(model, width, bw) {
    const tmp = document.createElement('canvas');
    const geo = Diagram.geometry(model, width);
    tmp.width = Math.round(geo.width); tmp.height = Math.round(geo.height);
    Diagram.draw(tmp.getContext('2d'), model, tmp.width, { bw });
    return tmp;
  }

  function exportJpg(model, name, bw, extraHeader) {
    const width = Math.min(4000, Math.max(1600, model.grid.cols * 110));
    const diag = renderCanvas(model, width, bw);
    const headH = Math.round(width * 0.05);
    const out = document.createElement('canvas');
    out.width = diag.width; out.height = diag.height + headH;
    const ctx = out.getContext('2d');
    ctx.fillStyle = bw ? '#fff' : '#1b1f27';
    ctx.fillRect(0, 0, out.width, out.height);
    ctx.fillStyle = bw ? '#000' : '#e8ecf2';
    ctx.font = `bold ${headH * 0.42}px system-ui, sans-serif`;
    ctx.textBaseline = 'middle';
    ctx.fillText(extraHeader, headH * 0.4, headH * 0.55);
    ctx.drawImage(diag, 0, headH);
    return saveBytes(`${safeName(name)}.jpg`, dataUrlToBytes(out.toDataURL('image/jpeg', 0.92)), 'image/jpeg');
  }

  let fontsB64 = null;
  async function loadFonts() {
    if (fontsB64) return fontsB64;
    const get = async f => {
      const r = await fetch(`fonts/${f}`);
      if (!r.ok) throw new Error('font');
      return bytesToBase64(new Uint8Array(await r.arrayBuffer()));
    };
    try {
      fontsB64 = { regular: await get('DejaVuSans.ttf'), bold: await get('DejaVuSans-Bold.ttf') };
    } catch (e) { fontsB64 = false; }
    return fontsB64;
  }

  /**
   * report = { title, subtitle, generated, summary: [[k,v]], ports: [[name, cab, px, pct]], cables: [[k,v]],
   *            settings: [[k,v]], headings: {summary, ports, cables, settings}, footer }
   */
  async function exportPdf(model, name, bw, report) {
    const { jsPDF } = window.jspdf;
    const doc = new jsPDF({ orientation: 'landscape', unit: 'mm', format: 'a4' });
    const fonts = await loadFonts();
    let F = 'helvetica';
    if (fonts) {
      doc.addFileToVFS('DejaVuSans.ttf', fonts.regular); doc.addFont('DejaVuSans.ttf', 'DejaVu', 'normal');
      doc.addFileToVFS('DejaVuSans-Bold.ttf', fonts.bold); doc.addFont('DejaVuSans-Bold.ttf', 'DejaVu', 'bold');
      F = 'DejaVu';
    }
    const PW = 297, PH = 210, M = 12;
    const accent = bw ? [0, 0, 0] : [30, 110, 210];

    const header = (pageTitle) => {
      doc.setFont(F, 'bold'); doc.setFontSize(16); doc.setTextColor(0, 0, 0);
      doc.text(report.title, M, M + 4);
      doc.setFont(F, 'normal'); doc.setFontSize(10); doc.setTextColor(80, 80, 80);
      doc.text(report.subtitle, M, M + 10);
      doc.text(report.generated, PW - M, M + 4, { align: 'right' });
      if (pageTitle) doc.text(pageTitle, PW - M, M + 10, { align: 'right' });
      doc.setDrawColor(accent[0], accent[1], accent[2]); doc.setLineWidth(0.6);
      doc.line(M, M + 13, PW - M, M + 13);
    };
    const footer = (n) => {
      doc.setFont(F, 'normal'); doc.setFontSize(8); doc.setTextColor(120, 120, 120);
      doc.text(report.footer, M, PH - 6);
      doc.text(String(n), PW - M, PH - 6, { align: 'right' });
    };

    // Page 1 — diagram
    header('');
    const diag = renderCanvas(model, 2400, bw);
    const availW = PW - 2 * M, availH = PH - (M + 18) - 14;
    const s = Math.min(availW / diag.width, availH / diag.height);
    const w = diag.width * s, h = diag.height * s;
    doc.addImage(diag.toDataURL('image/jpeg', 0.92), 'JPEG', M + (availW - w) / 2, M + 18, w, h);
    let page = 1;
    footer(page);

    // Page 2 — data
    doc.addPage();
    header('');
    const colW = (PW - 2 * M - 10) / 2;
    const table = (x, y, title, rows, widths) => {
      doc.setFont(F, 'bold'); doc.setFontSize(12); doc.setTextColor(accent[0], accent[1], accent[2]);
      doc.text(title, x, y); y += 1;
      doc.setFontSize(9.5); doc.setTextColor(20, 20, 20);
      rows.forEach((row, i) => {
        const lines = row.map((cell, j) => doc.splitTextToSize(String(cell), widths[j] - 3));
        const n = Math.max.apply(null, lines.map(l => l.length));
        const rh = 1.6 + n * 4;
        if (y + rh > PH - 14) { footer(++page); doc.addPage(); header(''); y = M + 22; doc.setFontSize(9.5); doc.setTextColor(20, 20, 20); }
        if (i % 2 === 0) { doc.setFillColor(242, 244, 247); doc.rect(x, y + 1.4, widths.reduce((a, b) => a + b, 0), rh, 'F'); }
        let cx = x + 1.5;
        lines.forEach((ls, j) => {
          doc.setFont(F, j === row.length - 1 ? 'bold' : 'normal');
          doc.text(ls, cx, y + 5);
          cx += widths[j];
        });
        y += rh;
      });
      return y + 8;
    };
    let yl = M + 22;
    yl = table(M, yl, report.headings.summary, report.summary, [colW * 0.48, colW * 0.52]);
    yl = table(M, yl, report.headings.cables, report.cables, [colW * 0.7, colW * 0.3]);
    let yr = M + 22;
    yr = table(M + colW + 10, yr, report.headings.ports, report.ports, [colW * 0.25, colW * 0.2, colW * 0.35, colW * 0.2]);
    table(M + colW + 10, yr, report.headings.settings, report.settings, [colW * 0.55, colW * 0.45]);
    footer(++page);

    const bytes = new Uint8Array(doc.output('arraybuffer'));
    return saveBytes(`${safeName(name)}.pdf`, bytes, 'application/pdf');
  }

  window.Exporter = { saveBytes, exportJpg, exportPdf, safeName };
})();
