/**
 * Service worker for the BratenDreher control app.
 *
 * Strategy: network-first with a cache fallback.
 *
 * The app is ~190 KB of static files served from a CDN, so caching buys very
 * little speed - the reason this service worker exists is *offline* use, since
 * the app talks to the controller over Bluetooth and is typically used standing
 * at a grill with poor signal.
 *
 * Network-first therefore fits better than cache-first: when there is a
 * connection you always get the current version (a plain reload is enough to
 * pick up a new deployment), and when there is not, the precached copy takes
 * over. A cache-first worker would instead pin users to whatever version they
 * first loaded, which is both surprising and hard to escape.
 *
 * CACHE_VERSION is replaced with the commit SHA by the GitHub Pages workflow.
 * Running from a plain file server it stays as the literal placeholder, which
 * is fine - with network-first the cache name no longer gates updates.
 */
const CACHE_VERSION = '__CACHE_VERSION__';
const CACHE_NAME = `bratendreher-${CACHE_VERSION}`;

// How long to wait for the network before falling back to the cache. Long
// enough for a slow connection, short enough that a dead one is not a hang.
const NETWORK_TIMEOUT_MS = 3000;

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
    event.waitUntil((async () => {
        const cache = await caches.open(CACHE_NAME);
        // Precache so the very first offline launch works even for files the
        // user has not happened to request yet.
        await cache.addAll(APP_SHELL);
        // Take over straight away. Safe here because the page loads all of its
        // assets up front and fetches nothing afterwards, so an activation
        // mid-session cannot swap code underneath a running page - and it does
        // not touch the Bluetooth connection either.
        await self.skipWaiting();
    })());
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

function fetchWithTimeout(request, timeoutMs) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('network timeout')), timeoutMs);
        fetch(request).then(
            (response) => { clearTimeout(timer); resolve(response); },
            (error) => { clearTimeout(timer); reject(error); }
        );
    });
}

self.addEventListener('fetch', (event) => {
    const request = event.request;

    if (request.method !== 'GET') return;
    if (new URL(request.url).origin !== self.location.origin) return;

    event.respondWith((async () => {
        const cache = await caches.open(CACHE_NAME);

        try {
            const response = await fetchWithTimeout(request, NETWORK_TIMEOUT_MS);
            if (response && response.ok && response.type === 'basic') {
                cache.put(request, response.clone());
            }
            return response;
        } catch (error) {
            const cached = await cache.match(request, { ignoreSearch: true });
            if (cached) return cached;

            // Offline and never cached. For a navigation the shell is still
            // the most useful thing to hand back.
            if (request.mode === 'navigate') {
                const shell = await cache.match('./index.html');
                if (shell) return shell;
            }
            throw error;
        }
    })());
});
