/* Offline cache for the web / home-screen version. Bump VERSION when assets change. */
const VERSION = 'ledmapper-86b5994';
const ASSETS = [
  "./",
  "./css/style.css",
  "./fonts/DejaVuSans-Bold.ttf",
  "./fonts/DejaVuSans.ttf",
  "./icons/apple-touch-icon.png",
  "./icons/icon-192.png",
  "./icons/icon-512.png",
  "./index.html",
  "./js/app.js",
  "./js/engine.js",
  "./js/export.js",
  "./js/i18n.js",
  "./js/render.js",
  "./js/scr.js",
  "./lib/jspdf.umd.min.js",
  "./manifest.webmanifest"
];
self.addEventListener('install', e => {
  e.waitUntil(caches.open(VERSION).then(c => c.addAll(ASSETS)).then(() => self.skipWaiting()));
});
self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});
self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET') return;
  e.respondWith(caches.match(e.request, { ignoreSearch: true }).then(hit => hit || fetch(e.request)));
});
