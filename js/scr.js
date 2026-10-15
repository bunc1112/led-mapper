/*
 * NovaLCT screen-connection (.scr) writer.
 *
 * Binary layout (little-endian), as decoded by the open-source @novastar/screen package
 * (https://github.com/sarakusha/novastar, MIT):
 *
 *  DSCI header (54 bytes)
 *    0  char[4]  "DSCI"
 *    4  u16      crc  = byte-sum (mod 65536) of bytes [6 .. end of screen info]
 *    6  u32      dviInfoLength
 *   10  u32      screenInfoLength
 *   14  u16      adjustInfoLength
 *   16  ...      reserved (zero) up to 54
 *  DVI info
 *    u16 version, u16 crc (byte-sum of following bytes), u8 dviCols, u8 dviRows,
 *    u16 graphicsWidth, u16 graphicsHeight
 *  Screen info
 *    0 u16 version, 2 u16 crc (byte-sum of bytes from 4), 4..131 reserved,
 *    132 u8 screenCount, 133 u32[screenCount] lengths, then screens
 *  Standard screen (type 1)
 *    u8 type=1, u8 virtualMode=0, u16 x, u16 y, u16 cols, u16 rows,
 *    then cols*rows region records (row-major):
 *      u8 sender, u8 port, u16 connectIndex, u16 x, u16 y, u16 xInPort, u16 yInPort,
 *      u16 width, u16 height, u8 dviIndex
 *  Screen adjust info
 *    u16 version(1002), reserved to 132, u8 count, count × 40-byte params:
 *      u8 xZoomType, u8 xScale, u8 yZoomType, u8 yScale, u8 virtualMap, char[20] name, u8 threeD, pad
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.ScrWriter = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const SCREEN_INFO_VERSION = 1005;
  const DVI_INFO_VERSION = 1001;
  const ADJUST_INFO_VERSION = 1002;

  function sum16(bytes, from, to) {
    let s = 0;
    for (let i = from; i < to; i++) s = (s + bytes[i]) & 0xffff;
    return s;
  }

  class W {
    constructor(n) { this.buf = new Uint8Array(n); this.dv = new DataView(this.buf.buffer); this.o = 0; }
    u8(v) { this.dv.setUint8(this.o, v & 0xff); this.o += 1; }
    u16(v) { this.dv.setUint16(this.o, v & 0xffff, true); this.o += 2; }
    u32(v) { this.dv.setUint32(this.o, v >>> 0, true); this.o += 4; }
    ascii(s, n) { for (let i = 0; i < n; i++) this.u8(i < s.length ? (s.charCodeAt(i) & 0x7f) : 0); }
    at(o) { this.o = o; return this; }
  }

  /**
   * @param grid   Engine.buildGrid result
   * @param layout Engine.buildLayout result
   * @param opts   { portsPerSender, name }
   * @returns Uint8Array
   */
  function build(grid, layout, opts) {
    const portsPerSender = Math.max(1, opts.portsPerSender || 4);
    const cols = grid.cols, rows = grid.rows.length;
    const colX = c => c * grid.pxW;

    // --- DVI info
    const dvi = new W(10);
    dvi.u16(DVI_INFO_VERSION); dvi.u16(0); dvi.u8(1); dvi.u8(1);
    dvi.u16(Math.min(65535, grid.widthPx)); dvi.u16(Math.min(65535, grid.heightPx));
    dvi.at(2).u16(sum16(dvi.buf, 4, 10));

    // --- Standard screen
    const REC = 17;
    const screenLen = 10 + cols * rows * REC;
    const sc = new W(screenLen);
    sc.u8(1); sc.u8(0); sc.u16(0); sc.u16(0); sc.u16(cols); sc.u16(rows);
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        const cell = layout.cells[r][c];
        const port = layout.ports[cell.port];
        const reg = port.region;
        const x = colX(c), y = grid.rows[r].y;
        sc.u8(Math.floor(cell.port / portsPerSender));
        sc.u8(cell.port % portsPerSender);
        sc.u16(cell.index);
        sc.u16(x); sc.u16(y);
        sc.u16(x - colX(reg.c0)); sc.u16(y - grid.rows[reg.r0].y);
        sc.u16(grid.pxW); sc.u16(grid.rows[r].px);
        sc.u8(0);
      }
    }

    // --- Screen info wrapper
    const infoLen = 133 + 4 + screenLen;
    const info = new W(infoLen);
    info.u16(SCREEN_INFO_VERSION); info.u16(0);
    info.at(132).u8(1); info.u32(screenLen);
    info.buf.set(sc.buf, 137);
    info.at(2).u16(sum16(info.buf, 4, infoLen));

    // --- Adjust info
    const adj = new W(133 + 40);
    adj.u16(ADJUST_INFO_VERSION);
    adj.at(132).u8(1);
    adj.u8(0); adj.u8(1); adj.u8(0); adj.u8(1); adj.u8(228);
    adj.ascii((opts.name || 'Screen1').replace(/[^\x20-\x7e]/g, '_'), 20);
    adj.u8(0);

    // --- DSCI container
    const total = 54 + dvi.buf.length + info.buf.length + adj.buf.length;
    const out = new W(total);
    out.ascii('DSCI', 4); out.u16(0);
    out.u32(dvi.buf.length); out.u32(info.buf.length); out.u16(adj.buf.length);
    let o = 54;
    out.buf.set(dvi.buf, o); o += dvi.buf.length;
    out.buf.set(info.buf, o); o += info.buf.length;
    out.buf.set(adj.buf, o);
    out.at(4).u16(sum16(out.buf, 6, total - adj.buf.length));
    return out.buf;
  }

  return { build };
});
