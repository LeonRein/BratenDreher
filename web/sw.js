/**
 * Service worker for the BratenDreher control app.
 *
 * The whole app is ~190 KB of static files with no backend, so the entire
 * shell is precached and served cache-first. That makes the app start
 * instantly and work with no network at all - which matters, because it talks
 * to the controller over Bluetooth and is typically used standing at a grill.
 *
 * CACHE_VERSION is replaced with the commit SHA by the GitHub Pages workflow.
 * When running from a plain file server it stays as the literal placeholder,
 * which is fine - it just means the cache name never changes locally.
 */
const CACHE_VERSION = '__CACHE_VERSION__';
const CACHE_NAME = `bratendreher-${CACHE_VERSION}`;

const APP_SHELL = [
    './',
    './index.html',
    './style.css',
    './msgpack.min.js',
    './controls.js',
    './command-manager.js',
    './control-bindings.js',
    './app.js',
    './manifest.webmanifest',
    './icons/icon.svg',
    './icons/icon-192.png',
    './icons/icon-512.png',
    './icons/apple-touch-icon.png'
];

self.addEventListener('install', (event) => {
    // Precache the complete shell so a version swap is atomic: the page never
    // ends up running new HTML against stale JS.
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => cache.addAll(APP_SHELL))
    );
    // Deliberately no skipWaiting(): a new version takes over only once the app
    // has been fully closed. Swapping assets under a running page would drop
    // the live Bluetooth connection mid-session.
});

self.addEventListener('activate', (event) => {
    event.waitUntil((async () => {
        const names = await caches.keys();
        await Promise.all(
            names
                .filter((name) => name.startsWith('bratendreher-') && name !== CACHE_NAME)
                .map((name) => caches.delete(name))
        );
        await self.clients.claim();
    })());
});

self.addEventListener('fetch', (event) => {
    const request = event.request;

    if (request.method !== 'GET') return;
    if (new URL(request.url).origin !== self.location.origin) return;

    event.respondWith((async () => {
        const cached = await caches.match(request, { ignoreSearch: true });
        if (cached) return cached;

        try {
            return await fetch(request);
        } catch (error) {
            // Offline and not in the cache. For a navigation, fall back to the
            // app shell so the UI still comes up.
            if (request.mode === 'navigate') {
                const shell = await caches.match('./index.html');
                if (shell) return shell;
            }
            throw error;
        }
    })());
});
