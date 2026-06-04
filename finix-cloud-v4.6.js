#!/usr/bin/env node
// =================================================================
// FINIX CLOUD v4.6 - ENHANCED EDITION
// Creator: Nurudeen Al Haitami (Alkha)
// Improvements: UI Polish, Security Hardened, Login Fix, Video Preview, Move File, No Default Creds
// =================================================================

const express = require('express');
const multer = require('multer');
const fs = require('fs-extra');
const path = require('path');
const session = require('express-session');
const FileStore = require('session-file-store')(session);
const bcrypt = require('bcryptjs');
const { v4: uuidv4 } = require('uuid');
const crypto = require('crypto');

const app = express();
const PORT = 8000;

// ======================== KONFIGURASI ========================
const HOME_DIR = process.env.HOME || '/data/data/com.termux/files/home';
const DATA_DIR = path.join(HOME_DIR, 'finix-cloud-data');
const USERS_FILE = path.join(DATA_DIR, 'users.json');
const STORAGE_DIR = path.join(HOME_DIR, 'finix-cloud-storage');

[STORAGE_DIR, DATA_DIR, path.join(DATA_DIR, 'sessions')].forEach(dir => {
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
});

// ======================== SESSION SECRET ========================
// Generate sekali dan simpan agar konsisten setelah restart
const SESSION_SECRET_FILE = path.join(DATA_DIR, '.session_secret');
let SESSION_SECRET;
if (fs.existsSync(SESSION_SECRET_FILE)) {
  SESSION_SECRET = fs.readFileSync(SESSION_SECRET_FILE, 'utf8').trim();
} else {
  SESSION_SECRET = crypto.randomBytes(64).toString('hex');
  fs.writeFileSync(SESSION_SECRET_FILE, SESSION_SECRET, { mode: 0o600 });
}

// ======================== MIDDLEWARE ========================
app.use(express.json({ limit: '2gb' }));
app.use(express.urlencoded({ extended: true, limit: '2gb' }));

app.use(session({
  store: new FileStore({
    path: path.join(DATA_DIR, 'sessions'),
    ttl: 86400,
    retries: 0,
    reapInterval: 3600
  }),
  secret: SESSION_SECRET,
  resave: false,
  saveUninitialized: false,
  name: 'finix.sid',
  cookie: {
    httpOnly: true,
    sameSite: 'strict',        // lebih ketat dari 'lax'
    maxAge: 24 * 60 * 60 * 1000,
    secure: false              // ganti true jika pakai HTTPS
  }
}));

// ======================== SECURITY HEADERS ========================
app.use((req, res, next) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('X-Frame-Options', 'DENY');
  res.setHeader('X-XSS-Protection', '1; mode=block');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('Permissions-Policy', 'camera=(), microphone=(), geolocation=()');
  res.setHeader('Content-Security-Policy',
    "default-src 'self'; " +
    "script-src 'self' 'unsafe-inline' https://fonts.googleapis.com; " +
    "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com https://fonts.gstatic.com; " +
    "font-src 'self' https://fonts.gstatic.com; " +
    "img-src 'self' data: blob:; " +
    "media-src 'self' blob:; " +
    "frame-src 'self';"
  );
  next();
});

// ======================== RATE LIMITER (lebih canggih) ========================
const rateLimit = new Map();
const LOGIN_FAIL_MAP = new Map(); // track login failures per IP

function rateLimiter(maxRequests, windowMs) {
  return (req, res, next) => {
    const ip = req.ip;
    const now = Date.now();
    if (!rateLimit.has(ip)) rateLimit.set(ip, []);
    const requests = rateLimit.get(ip).filter(t => now - t < windowMs);
    if (requests.length >= maxRequests) {
      return res.status(429).send(errorPage('429 - Too Many Requests', 'Terlalu banyak permintaan. Coba lagi nanti.'));
    }
    requests.push(now);
    rateLimit.set(ip, requests);
    next();
  };
}

// Bersihkan rate limit map setiap 5 menit
setInterval(() => {
  const now = Date.now();
  for (const [ip, times] of rateLimit.entries()) {
    const filtered = times.filter(t => now - t < 60000);
    if (filtered.length === 0) rateLimit.delete(ip);
    else rateLimit.set(ip, filtered);
  }
  for (const [ip, data] of LOGIN_FAIL_MAP.entries()) {
    if (now - data.lastAttempt > 15 * 60 * 1000) LOGIN_FAIL_MAP.delete(ip);
  }
}, 5 * 60 * 1000);

app.use(rateLimiter(60, 60000)); // 60 req/menit global

// ======================== MANAJEMEN USER ========================
let users = {};
if (fs.existsSync(USERS_FILE)) {
  try { users = JSON.parse(fs.readFileSync(USERS_FILE, 'utf8')); } catch(e) { users = {}; }
}

// Tidak ada akun default — user wajib setup sendiri via /setup

function saveUsers() {
  fs.writeFileSync(USERS_FILE, JSON.stringify(users, null, 2));
}

// ======================== SETUP GUARD ========================
// Kalau belum ada user sama sekali, redirect ke /setup
app.use((req, res, next) => {
  const isSetupRoute = req.path === '/setup' || req.path.startsWith('/setup');
  if (Object.keys(users).length === 0 && !isSetupRoute) {
    return res.redirect('/setup');
  }
  next();
});

// ======================== HALAMAN SETUP PERTAMA KALI ========================
app.get('/setup', (req, res) => {
  // Kalau sudah ada user, setup tidak bisa diakses lagi
  if (Object.keys(users).length > 0) return res.redirect('/login');
  res.send(`<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Setup - Finix Cloud</title>
<style>${getSharedCSS()}
.card{max-width:460px}
.badge{display:inline-block;background:#4f46e522;color:#4f46e5;padding:4px 12px;border-radius:20px;font-size:12px;font-weight:600;margin-bottom:20px}
.warning{background:#fef3c7;border:1px solid #fbbf24;color:#92400e;padding:12px 16px;border-radius:12px;font-size:13px;margin-bottom:20px;line-height:1.5}
</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <h1>🛠️ Setup Awal</h1>
    <div class="sub">Buat akun pertama (admin) untuk mulai</div>
  </div>
  <div class="badge">🔒 Hanya bisa diakses sekali</div>
  <div class="warning">
    ⚠️ Halaman ini hanya muncul sekali saat instalasi pertama.<br>
    Setelah akun dibuat, halaman ini tidak bisa diakses lagi.
  </div>
  <form id="setupForm" method="POST" action="/setup">
    <div class="field">
      <label>Username Admin</label>
      <input type="text" name="username" placeholder="Buat username unik" required minlength="3" maxlength="30" pattern="[a-zA-Z0-9_]+" title="Hanya huruf, angka, underscore" autofocus autocomplete="username">
    </div>
    <div class="field">
      <label>Password</label>
      <input type="password" name="password" placeholder="Min. 8 karakter" required minlength="8" maxlength="72" autocomplete="new-password">
    </div>
    <div class="field">
      <label>Konfirmasi Password</label>
      <input type="password" name="password2" placeholder="Ulangi password" required minlength="8" maxlength="72" autocomplete="new-password">
    </div>
    <div class="field">
      <label>Kuota Storage</label>
      <select name="quota">
        <option value="1">1 GB — Basic</option>
        <option value="2">2 GB — Starter</option>
        <option value="4">4 GB — Standard</option>
        <option value="8" selected>8 GB — Premium</option>
      </select>
    </div>
    <button type="submit" class="btn-primary" id="setupBtn">Buat Akun & Mulai →</button>
  </form>
</div>
<script>
document.getElementById('setupForm').addEventListener('submit', function(e) {
  const p1 = this.password.value;
  const p2 = this.password2.value;
  if (p1 !== p2) { e.preventDefault(); alert('Password tidak cocok!'); return; }
  const btn = document.getElementById('setupBtn');
  btn.disabled = true;
  btn.textContent = 'Membuat akun...';
});
</script>
</body>
</html>`);
});

app.post('/setup', async (req, res) => {
  // Kalau sudah ada user, tolak
  if (Object.keys(users).length > 0) return res.redirect('/login');

  const { username, password, password2, quota } = req.body;
  if (!username || !password || !password2) return res.redirect('/setup');
  if (!/^[a-zA-Z0-9_]{3,30}$/.test(username)) return res.redirect('/setup');
  if (password.length < 8 || password.length > 72) return res.redirect('/setup');
  if (password !== password2) return res.redirect('/setup');

  const quotaGB = Math.min(Math.max(parseInt(quota) || 8, 1), 8);
  const userId = uuidv4();
  users[userId] = {
    id: userId,
    username,
    password: await bcrypt.hash(password, 12),
    quota: quotaGB * 1024 * 1024 * 1024,
    createdAt: new Date().toISOString()
  };
  fs.mkdirSync(path.join(STORAGE_DIR, userId), { recursive: true });
  saveUsers();

  console.log(`[SETUP] Akun pertama dibuat: ${username}`);
  res.redirect('/login');
});

// ======================== PATH TRAVERSAL PROTECTION ========================
function safePath(userDir, relativePath) {
  if (!relativePath) return userDir;
  // Hapus karakter berbahaya
  const cleaned = relativePath.replace(/\0/g, '').replace(/\.\./g, '');
  const resolved = path.resolve(path.join(userDir, cleaned));
  // Pastikan path masih di dalam userDir
  if (!resolved.startsWith(path.resolve(userDir))) return null;
  return resolved;
}

// ======================== FUNGSI BANTU ========================
function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#x27;');
}

function getFileIcon(filename) {
  const ext = path.extname(filename).toLowerCase();
  const icons = {
    '.jpg': '🖼️', '.jpeg': '🖼️', '.png': '🖼️', '.gif': '🖼️', '.webp': '🖼️', '.svg': '🖼️',
    '.mp4': '🎬', '.webm': '🎬', '.mov': '🎬', '.avi': '🎬', '.mkv': '🎬',
    '.mp3': '🎵', '.wav': '🎵', '.ogg': '🎵', '.flac': '🎵', '.aac': '🎵',
    '.pdf': '📕',
    '.txt': '📄', '.md': '📝',
    '.js': '💻', '.ts': '💻', '.py': '🐍', '.html': '🌐', '.css': '🎨',
    '.json': '🔧', '.xml': '🔧', '.yaml': '🔧', '.yml': '🔧',
    '.zip': '📦', '.rar': '📦', '.tar': '📦', '.gz': '📦',
    '.apk': '📱', '.exe': '⚙️', '.sh': '⚙️',
    '.doc': '📃', '.docx': '📃', '.xls': '📊', '.xlsx': '📊', '.ppt': '📋', '.pptx': '📋'
  };
  return icons[ext] || '📄';
}

function getMimeType(filename) {
  const ext = path.extname(filename).toLowerCase();
  const mimes = {
    '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.png': 'image/png',
    '.gif': 'image/gif', '.webp': 'image/webp', '.svg': 'image/svg+xml',
    '.mp4': 'video/mp4', '.webm': 'video/webm', '.mov': 'video/quicktime',
    '.avi': 'video/x-msvideo', '.mkv': 'video/x-matroska',
    '.mp3': 'audio/mpeg', '.wav': 'audio/wav', '.ogg': 'audio/ogg',
    '.flac': 'audio/flac', '.aac': 'audio/aac',
    '.pdf': 'application/pdf',
    '.txt': 'text/plain', '.md': 'text/markdown',
    '.html': 'text/html', '.css': 'text/css',
    '.js': 'application/javascript', '.ts': 'application/typescript',
    '.json': 'application/json', '.xml': 'application/xml'
  };
  return mimes[ext] || 'application/octet-stream';
}

function isVideoFile(filename) {
  return ['.mp4', '.webm', '.mov', '.avi', '.mkv'].includes(path.extname(filename).toLowerCase());
}

function isAudioFile(filename) {
  return ['.mp3', '.wav', '.ogg', '.flac', '.aac'].includes(path.extname(filename).toLowerCase());
}

function isImageFile(filename) {
  return ['.jpg', '.jpeg', '.png', '.gif', '.webp', '.svg'].includes(path.extname(filename).toLowerCase());
}

function isTextFile(filename) {
  return ['.txt', '.md', '.js', '.ts', '.py', '.html', '.css', '.json', '.xml', '.yaml', '.yml', '.sh', '.env'].includes(path.extname(filename).toLowerCase());
}

function isPreviewable(filename) {
  return isVideoFile(filename) || isAudioFile(filename) || isImageFile(filename) ||
    isTextFile(filename) || path.extname(filename).toLowerCase() === '.pdf';
}

function getFolderSize(dir) {
  if (!fs.existsSync(dir)) return 0;
  let total = 0;
  try {
    const items = fs.readdirSync(dir);
    for (const item of items) {
      const itemPath = path.join(dir, item);
      try {
        const stat = fs.statSync(itemPath);
        if (stat.isDirectory()) total += getFolderSize(itemPath);
        else total += stat.size;
      } catch(e) {}
    }
  } catch(e) {}
  return total;
}

function requireLogin(req, res, next) {
  if (!req.session.userId || !users[req.session.userId]) {
    return res.redirect('/login');
  }
  next();
}

function errorPage(title, message) {
  return `<!DOCTYPE html><html><head><meta charset="UTF-8"><title>${escapeHtml(title)}</title>
  <style>body{font-family:sans-serif;background:#0a0f1a;color:#e2e8f0;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}
  .box{text-align:center;padding:40px;background:#1a1f2e;border-radius:20px;max-width:400px}
  h1{color:#e53e3e;font-size:28px;margin-bottom:16px}p{color:#8892b0;margin-bottom:24px}
  a{color:#4f46e5;text-decoration:none}</style></head>
  <body><div class="box"><h1>${escapeHtml(title)}</h1><p>${escapeHtml(message)}</p><a href="/">← Kembali</a></div></body></html>`;
}

// ======================== MULTER UPLOAD ========================
const upload = multer({
  storage: multer.diskStorage({
    destination: (req, file, cb) => {
      const userDir = path.join(STORAGE_DIR, req.session.userId);
      const currentPath = req.body.currentPath || '';
      const targetDir = safePath(userDir, currentPath);
      if (!targetDir) return cb(new Error('Invalid path'), null);
      if (!fs.existsSync(targetDir)) fs.mkdirSync(targetDir, { recursive: true });
      cb(null, targetDir);
    },
    filename: (req, file, cb) => {
      // Sanitasi nama file - hanya izinkan karakter aman
      const ext = path.extname(file.originalname).replace(/[^a-zA-Z0-9.]/g, '');
      const baseName = path.basename(file.originalname, path.extname(file.originalname))
        .replace(/[^a-zA-Z0-9._\-\s]/g, '_')
        .replace(/\s+/g, '_')
        .substring(0, 100);
      const timestamp = Date.now();
      cb(null, `${baseName}_${timestamp}${ext}`);
    }
  }),
  limits: { fileSize: 2 * 1024 * 1024 * 1024 }, // 2GB max
  fileFilter: (req, file, cb) => {
    // Blokir file ekstensi berbahaya
    const dangerousExts = ['.exe', '.bat', '.cmd', '.com', '.vbs', '.ps1', '.msi', '.dll'];
    const ext = path.extname(file.originalname).toLowerCase();
    if (dangerousExts.includes(ext)) {
      return cb(new Error('Tipe file tidak diizinkan'), false);
    }
    cb(null, true);
  }
});

// ======================== TEMA WARNA ========================
const themes = {
  default: { name: '🌙 Dark', primary: '#4f46e5', bg: '#0a0f1a', cardBg: '#1a1f2e', text: '#e2e8f0', textSecondary: '#8892b0', border: '#2a2f3f', accent: '#2ecc71' },
  white:   { name: '☀️ White', primary: '#3b82f6', bg: '#f0f2f5', cardBg: '#ffffff', text: '#1a1a2e', textSecondary: '#666666', border: '#e0e0e0', accent: '#2ecc71' },
  yellow:  { name: '💛 Gold', primary: '#eab308', bg: '#1a1a0a', cardBg: '#2a2a10', text: '#ffd700', textSecondary: '#cccc00', border: '#3a3a00', accent: '#ffd700' },
  purple:  { name: '💜 Purple', primary: '#a855f7', bg: '#1a0a2e', cardBg: '#2a1040', text: '#ede9fe', textSecondary: '#a0a0cc', border: '#3a1a5a', accent: '#bb86fc' },
  ocean:   { name: '🌊 Ocean', primary: '#06b6d4', bg: '#0a1a2e', cardBg: '#0c2a4a', text: '#e0f2fe', textSecondary: '#7dd3fc', border: '#0a3a5a', accent: '#4fc3f7' }
};

// ======================== CSS SHARED ========================
function getSharedCSS() {
  return `
    @import url('https://fonts.googleapis.com/css2?family=Sora:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap');
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:'Sora',sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
    .card{background:rgba(255,255,255,0.97);backdrop-filter:blur(20px);padding:40px 32px;border-radius:28px;width:100%;max-width:420px;box-shadow:0 30px 60px rgba(0,0,0,0.2),0 0 0 1px rgba(255,255,255,0.3)}
    .logo{text-align:center;margin-bottom:28px}
    .logo h1{font-size:28px;font-weight:700;color:#4f46e5;letter-spacing:-0.5px}
    .logo .sub{color:#888;font-size:14px;margin-top:4px}
    .field{position:relative;margin-bottom:16px}
    .field label{display:block;font-size:13px;font-weight:600;color:#444;margin-bottom:6px;letter-spacing:0.3px}
    .field input,.field select{width:100%;padding:14px 16px;border:2px solid #e5e7eb;border-radius:14px;font-size:15px;font-family:'Sora',sans-serif;transition:all 0.2s;outline:none;background:#fafafa;color:#1a1a2e}
    .field input:focus,.field select:focus{border-color:#4f46e5;background:#fff;box-shadow:0 0 0 4px rgba(79,70,229,0.1)}
    .btn-primary{width:100%;padding:15px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:14px;font-weight:700;cursor:pointer;font-size:16px;font-family:'Sora',sans-serif;letter-spacing:0.3px;transition:all 0.2s;margin-top:8px}
    .btn-primary:hover{transform:translateY(-1px);box-shadow:0 8px 20px rgba(102,126,234,0.4)}
    .btn-primary:active{transform:translateY(0)}
    .link{text-align:center;margin-top:20px;font-size:14px;color:#888}
    .link a{color:#4f46e5;text-decoration:none;font-weight:600}
    .error-msg{background:#fee2e2;color:#dc2626;padding:12px 16px;border-radius:12px;font-size:14px;margin-bottom:16px;border:1px solid #fca5a5}
  `;
}

// ======================== HALAMAN LOGIN ========================
app.get('/login', (req, res) => {
  if (req.session.userId && users[req.session.userId]) return res.redirect('/');
  const err = req.query.err;
  const errMsg = err === '1' ? 'Username atau password salah.' :
                 err === '2' ? 'Terlalu banyak percobaan login. Tunggu 15 menit.' : '';
  res.send(`<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Login - Finix Cloud</title>
<style>${getSharedCSS()}</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <h1>☁️ Finix Cloud</h1>
    <div class="sub">Secure Personal Storage</div>
  </div>
  ${errMsg ? `<div class="error-msg">⚠️ ${escapeHtml(errMsg)}</div>` : ''}
  <form id="loginForm" method="POST" action="/login">
    <div class="field">
      <label>Username</label>
      <input type="text" name="username" placeholder="Masukkan username" required autofocus autocomplete="username">
    </div>
    <div class="field">
      <label>Password</label>
      <input type="password" name="password" placeholder="Masukkan password" required autocomplete="current-password">
    </div>
    <button type="submit" class="btn-primary" id="loginBtn">Masuk →</button>
  </form>
  <div class="link">Belum punya akun? <a href="/register">Daftar sekarang</a></div>
</div>
<script>
// Submit sekali saja, langsung tanpa double
document.getElementById('loginForm').addEventListener('submit', function(e) {
  const btn = document.getElementById('loginBtn');
  btn.disabled = true;
  btn.textContent = 'Memproses...';
});
</script>
</body>
</html>`);
});

// FIX: Login langsung masuk tanpa perlu 2 kali
app.post('/login', rateLimiter(10, 60000), async (req, res) => {
  const ip = req.ip;
  const { username, password } = req.body;

  // Cek brute force per IP
  const failData = LOGIN_FAIL_MAP.get(ip) || { count: 0, lastAttempt: 0 };
  if (failData.count >= 5) {
    const elapsed = Date.now() - failData.lastAttempt;
    if (elapsed < 15 * 60 * 1000) {
      return res.redirect('/login?err=2');
    } else {
      LOGIN_FAIL_MAP.delete(ip);
    }
  }

  if (!username || !password) return res.redirect('/login?err=1');

  const user = Object.values(users).find(u => u.username === username);
  if (!user) {
    // Tetap hash untuk mencegah timing attack
    await bcrypt.hash(password, 12);
    LOGIN_FAIL_MAP.set(ip, { count: (failData.count || 0) + 1, lastAttempt: Date.now() });
    return res.redirect('/login?err=1');
  }

  const valid = await bcrypt.compare(password, user.password);
  if (!valid) {
    LOGIN_FAIL_MAP.set(ip, { count: (failData.count || 0) + 1, lastAttempt: Date.now() });
    return res.redirect('/login?err=1');
  }

  // Reset fail counter on success
  LOGIN_FAIL_MAP.delete(ip);

  // FIX UTAMA: Regenerate session lalu LANGSUNG set userId sebelum redirect
  req.session.regenerate((err) => {
    if (err) return res.redirect('/login?err=1');
    req.session.userId = user.id;
    req.session.theme = req.session.theme || 'default';
    // Paksa save session dulu baru redirect
    req.session.save((saveErr) => {
      if (saveErr) return res.redirect('/login?err=1');
      res.redirect('/');
    });
  });
});

// ======================== REGISTER ========================
app.get('/register', (req, res) => {
  if (req.session.userId && users[req.session.userId]) return res.redirect('/');
  res.send(`<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Daftar - Finix Cloud</title>
<style>
${getSharedCSS()}
.card{max-width:440px}
</style>
</head>
<body>
<div class="card">
  <div class="logo">
    <h1>✨ Finix Cloud</h1>
    <div class="sub">Buat akun baru</div>
  </div>
  <form id="regForm" method="POST" action="/register">
    <div class="field">
      <label>Username</label>
      <input type="text" name="username" placeholder="Min. 3 karakter" required minlength="3" maxlength="30" pattern="[a-zA-Z0-9_]+" title="Hanya huruf, angka, dan underscore" autocomplete="username">
    </div>
    <div class="field">
      <label>Password</label>
      <input type="password" name="password" placeholder="Min. 6 karakter" required minlength="6" maxlength="72" autocomplete="new-password">
    </div>
    <div class="field">
      <label>Konfirmasi Password</label>
      <input type="password" name="password2" placeholder="Ulangi password" required minlength="6" maxlength="72" autocomplete="new-password">
    </div>
    <div class="field">
      <label>Kuota Storage</label>
      <select name="quota">
        <option value="1">1 GB — Basic</option>
        <option value="2">2 GB — Starter</option>
        <option value="4">4 GB — Standard</option>
        <option value="8" selected>8 GB — Premium</option>
      </select>
    </div>
    <button type="submit" class="btn-primary" id="regBtn">Daftar →</button>
  </form>
  <div class="link"><a href="/login">← Kembali ke Login</a></div>
</div>
<script>
document.getElementById('regForm').addEventListener('submit', function(e) {
  const p1 = this.password.value;
  const p2 = this.password2.value;
  if (p1 !== p2) {
    e.preventDefault();
    alert('Password tidak cocok!');
    return;
  }
  const btn = document.getElementById('regBtn');
  btn.disabled = true;
  btn.textContent = 'Mendaftar...';
});
</script>
</body>
</html>`);
});

app.post('/register', async (req, res) => {
  const { username, password, password2, quota } = req.body;

  // Validasi input ketat
  if (!username || !password || !password2) return res.redirect('/register?err=invalid');
  if (!/^[a-zA-Z0-9_]{3,30}$/.test(username)) return res.redirect('/register?err=invalid');
  if (password.length < 6 || password.length > 72) return res.redirect('/register?err=weak');
  if (password !== password2) return res.redirect('/register?err=mismatch');

  if (Object.values(users).some(u => u.username === username)) {
    return res.send(`<!DOCTYPE html><html><head><meta charset="UTF-8"><style>${getSharedCSS()}</style></head>
    <body><div class="card"><div class="logo"><h1>☁️ Finix Cloud</h1></div>
    <div class="error-msg">⚠️ Username sudah digunakan.</div>
    <div class="link"><a href="/register">← Coba username lain</a></div></div></body></html>`);
  }

  const quotaGB = Math.min(Math.max(parseInt(quota) || 1, 1), 8);
  const userId = uuidv4();
  users[userId] = {
    id: userId,
    username,
    password: await bcrypt.hash(password, 12),
    quota: quotaGB * 1024 * 1024 * 1024,
    createdAt: new Date().toISOString()
  };
  fs.mkdirSync(path.join(STORAGE_DIR, userId), { recursive: true });
  saveUsers();
  res.redirect('/login');
});

// ======================== DASHBOARD UTAMA ========================
app.get('/', requireLogin, (req, res) => {
  const user = users[req.session.userId];
  const currentPath = req.query.path || '';
  const userDir = path.join(STORAGE_DIR, user.id);
  const targetDir = safePath(userDir, currentPath);

  if (!targetDir) return res.status(400).send(errorPage('Path Tidak Valid', 'Path yang diminta tidak valid.'));
  if (!fs.existsSync(targetDir)) fs.mkdirSync(targetDir, { recursive: true });

  let items;
  try { items = fs.readdirSync(targetDir); }
  catch(e) { return res.status(500).send(errorPage('Error', 'Gagal membaca direktori.')); }

  const folders = [], files = [];
  for (const item of items) {
    const itemPath = path.join(targetDir, item);
    try {
      const stat = fs.statSync(itemPath);
      const relativePath = currentPath ? `${currentPath}/${item}` : item;
      if (stat.isDirectory()) folders.push({ name: item, path: relativePath });
      else files.push({ name: item, path: relativePath, size: stat.size, mtime: stat.mtime });
    } catch(e) {}
  }
  folders.sort((a, b) => a.name.localeCompare(b.name));
  files.sort((a, b) => a.name.localeCompare(b.name));

  const breadcrumbs = [{ name: 'Home', path: '' }];
  if (currentPath) {
    let buildPath = '';
    currentPath.split('/').forEach(part => {
      buildPath = buildPath ? `${buildPath}/${part}` : part;
      breadcrumbs.push({ name: part, path: buildPath });
    });
  }

  const storageUsed = getFolderSize(userDir);
  const storagePercent = Math.min((storageUsed / user.quota) * 100, 100);
  const parentPath = currentPath ? currentPath.split('/').slice(0, -1).join('/') : '';
  const theme = themes[req.session.theme] || themes.default;
  const totalItems = folders.length + files.length;

  res.send(`<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=yes">
<title>☁️ Finix Cloud — ${escapeHtml(user.username)}</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Sora:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap');
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Sora',sans-serif;background:${theme.bg};color:${theme.text};padding:14px;min-height:100vh}
.container{max-width:960px;margin:0 auto}

/* HEADER */
.header{background:${theme.cardBg};border-radius:20px;padding:14px 20px;margin-bottom:14px;
  display:flex;justify-content:space-between;align-items:center;
  border:1px solid ${theme.border};box-shadow:0 2px 12px rgba(0,0,0,0.08)}
.header-left{display:flex;align-items:center;gap:12px}
.header h1{color:${theme.accent};font-size:20px;font-weight:700;letter-spacing:-0.3px}
.header-right{display:flex;align-items:center;gap:10px}
.user-badge{background:${theme.primary}22;color:${theme.primary};padding:6px 12px;border-radius:20px;font-size:13px;font-weight:600}
.btn-logout{background:#e53e3e22;color:#e53e3e;border:1px solid #e53e3e44;padding:6px 14px;border-radius:20px;font-size:13px;font-weight:600;cursor:pointer;text-decoration:none;transition:0.2s}
.btn-logout:hover{background:#e53e3e;color:white}

/* STORAGE CARD */
.storage-card{background:${theme.cardBg};border-radius:16px;padding:14px 18px;margin-bottom:14px;
  border:1px solid ${theme.border};display:flex;gap:16px;align-items:center}
.storage-info{flex:1}
.storage-text{color:${theme.text};font-size:13px;font-weight:600;margin-bottom:6px}
.storage-bar{height:6px;background:${theme.border};border-radius:3px;overflow:hidden}
.storage-fill{height:100%;background:linear-gradient(90deg,${theme.primary},${theme.accent});
  width:${storagePercent.toFixed(1)}%;transition:width 0.5s;border-radius:3px}
.storage-pct{color:${theme.textSecondary};font-size:11px;margin-top:5px}
.theme-btn{background:${theme.cardBg};border:1px solid ${theme.border};color:${theme.textSecondary};
  padding:8px 12px;border-radius:12px;cursor:pointer;font-size:18px;transition:0.2s}
.theme-btn:hover{border-color:${theme.primary};color:${theme.primary}}

/* BREADCRUMB */
.breadcrumb{background:${theme.cardBg};border-radius:14px;padding:10px 18px;margin-bottom:14px;
  color:${theme.textSecondary};font-size:13px;border:1px solid ${theme.border};word-break:break-word}
.breadcrumb a{color:${theme.accent};text-decoration:none;font-weight:500}
.breadcrumb a:hover{text-decoration:underline}

/* TOOLBAR */
.toolbar{display:flex;gap:10px;margin-bottom:18px;flex-wrap:wrap}
.upload-area{flex:1;background:${theme.cardBg};border:2px dashed ${theme.border};border-radius:14px;
  padding:10px 14px;display:flex;gap:10px;align-items:center;flex-wrap:wrap;
  transition:0.2s;min-width:0}
.upload-area:hover,.upload-area.dragover{border-color:${theme.accent};background:${theme.accent}11}
.file-input-label{flex:1;color:${theme.textSecondary};font-size:13px;cursor:pointer;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#fileInputHidden{display:none}
.btn{background:${theme.accent};color:#fff;border:none;border-radius:12px;padding:10px 18px;
  font-weight:600;cursor:pointer;font-size:14px;font-family:'Sora',sans-serif;
  transition:all 0.2s;white-space:nowrap}
.btn:hover{opacity:0.88;transform:translateY(-1px)}
.btn:active{transform:translateY(0)}
.btn-folder{background:${theme.primary}}
.btn-sm{padding:7px 14px;font-size:13px}

/* STATS BAR */
.stats{color:${theme.textSecondary};font-size:12px;margin-bottom:12px;padding:0 2px}

/* GRID */
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px}
.item{background:${theme.cardBg};border-radius:16px;padding:14px 10px;text-align:center;
  border:1px solid ${theme.border};position:relative;cursor:pointer;
  transition:all 0.2s;user-select:none}
.item:hover{transform:translateY(-2px);border-color:${theme.primary};box-shadow:0 6px 20px rgba(0,0,0,0.1)}
.item:active{transform:scale(0.97)}
.icon{font-size:44px;margin-bottom:8px;line-height:1}
.name{color:${theme.text};font-size:12px;font-weight:500;word-break:break-all;
  margin-bottom:4px;line-height:1.4;max-height:36px;overflow:hidden;
  display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical}
.size{color:${theme.textSecondary};font-size:11px}
.item-back .icon{font-size:36px}

/* MENU DOT */
.menu-dots{position:absolute;top:6px;right:6px;background:${theme.border};
  border-radius:20px;padding:3px 8px;cursor:pointer;font-size:16px;
  color:${theme.textSecondary};z-index:10;opacity:0;transition:0.2s;line-height:1}
.item:hover .menu-dots{opacity:1}
.menu-dots:hover{background:${theme.primary};color:white}

/* ACTION MENU */
.action-menu{display:none;position:fixed;background:${theme.cardBg};border-radius:14px;
  padding:6px 0;min-width:150px;z-index:2000;
  box-shadow:0 8px 30px rgba(0,0,0,0.2);border:1px solid ${theme.border}}
.action-menu a{display:flex;align-items:center;gap:8px;padding:10px 16px;
  color:${theme.text};text-decoration:none;font-size:13px;font-weight:500;cursor:pointer;transition:0.15s}
.action-menu a:hover{background:${theme.primary}22;color:${theme.primary}}
.action-menu .divider{height:1px;background:${theme.border};margin:4px 0}
.action-menu .danger{color:#e53e3e}
.action-menu .danger:hover{background:#e53e3e22;color:#e53e3e}

/* MODAL */
.modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.6);
  z-index:3000;align-items:center;justify-content:center;padding:16px}
.modal-overlay.active{display:flex}
.modal{background:${theme.cardBg};border-radius:20px;padding:24px;width:100%;max-width:420px;
  border:1px solid ${theme.border};box-shadow:0 20px 60px rgba(0,0,0,0.3)}
.modal h3{color:${theme.text};font-size:17px;font-weight:700;margin-bottom:16px}
.modal input{width:100%;background:${theme.bg};border:2px solid ${theme.border};
  border-radius:12px;padding:12px 14px;color:${theme.text};font-size:15px;
  font-family:'Sora',sans-serif;outline:none;transition:0.2s}
.modal input:focus{border-color:${theme.primary}}
.modal-actions{display:flex;gap:10px;margin-top:16px;justify-content:flex-end}
.btn-cancel{background:transparent;border:1px solid ${theme.border};color:${theme.textSecondary}}

/* PREVIEW MODAL */
.preview-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.92);
  z-index:4000;align-items:center;justify-content:center;flex-direction:column;padding:16px}
.preview-overlay.active{display:flex}
.preview-header{width:100%;max-width:900px;display:flex;justify-content:space-between;
  align-items:center;margin-bottom:12px;padding:0 4px}
.preview-title{color:white;font-size:14px;font-weight:600;truncate;max-width:calc(100% - 50px);
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.preview-close{background:rgba(255,255,255,0.15);border:none;color:white;
  width:36px;height:36px;border-radius:50%;cursor:pointer;font-size:18px;
  display:flex;align-items:center;justify-content:center;transition:0.2s}
.preview-close:hover{background:rgba(255,255,255,0.3)}
.preview-body{width:100%;max-width:900px;max-height:80vh;overflow:auto;
  display:flex;align-items:center;justify-content:center}
.preview-body img{max-width:100%;max-height:80vh;border-radius:12px;object-fit:contain}
.preview-body video{width:100%;max-height:80vh;border-radius:12px;
  background:#000;outline:none}
.preview-body audio{width:100%;margin-top:24px}
.preview-body pre{background:#0d1117;color:#e6edf3;padding:20px;border-radius:12px;
  font-family:'JetBrains Mono',monospace;font-size:13px;
  width:100%;overflow:auto;max-height:75vh;white-space:pre-wrap;word-break:break-all}
.preview-body iframe{width:100%;height:75vh;border:none;border-radius:12px;background:white}
.preview-body .no-preview{color:#888;text-align:center;font-size:16px}
.preview-loader{color:white;font-size:16px;display:flex;align-items:center;gap:12px}
.spinner{width:24px;height:24px;border:3px solid rgba(255,255,255,0.2);
  border-top-color:white;border-radius:50%;animation:spin 0.8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.preview-footer{color:rgba(255,255,255,0.5);font-size:12px;margin-top:12px}

/* VIDEO CONTROLS CUSTOM */
video::-webkit-media-controls{border-radius:8px}

/* MOVE FOLDER LIST */
.move-folder-item{display:flex;align-items:center;gap:10px;padding:10px 14px;
  cursor:pointer;color:${theme.text};font-size:14px;border-bottom:1px solid ${theme.border};
  transition:0.15s;user-select:none}
.move-folder-item:last-child{border-bottom:none}
.move-folder-item:hover{background:${theme.primary}22;color:${theme.primary}}
.move-folder-item.selected{background:${theme.primary}33;color:${theme.primary};font-weight:600}
.move-folder-item.up-item{color:${theme.textSecondary};font-style:italic}
.move-folder-item.current-dir{color:${theme.accent};font-weight:600}
.move-empty{padding:24px;text-align:center;color:${theme.textSecondary};font-size:13px}



/* EMPTY STATE */
.empty-state{text-align:center;padding:60px 20px;color:${theme.textSecondary}}
.empty-state .empty-icon{font-size:64px;margin-bottom:16px;opacity:0.5}
.empty-state h3{font-size:18px;font-weight:600;margin-bottom:8px;color:${theme.text};opacity:0.7}
.empty-state p{font-size:14px}

/* UPLOAD PROGRESS */
.upload-progress{display:none;background:${theme.cardBg};border:1px solid ${theme.border};
  border-radius:14px;padding:14px 18px;margin-bottom:14px}
.upload-progress.active{display:block}
.progress-bar{height:6px;background:${theme.border};border-radius:3px;overflow:hidden;margin-top:8px}
.progress-fill{height:100%;background:linear-gradient(90deg,${theme.primary},${theme.accent});
  width:0%;transition:width 0.3s;border-radius:3px}
.progress-text{font-size:13px;color:${theme.textSecondary}}

.footer{text-align:center;margin-top:28px;color:${theme.textSecondary};font-size:12px;padding-bottom:20px}

/* THEME SELECTOR */
.theme-panel{display:none;position:fixed;top:70px;right:20px;
  background:${theme.cardBg};border:1px solid ${theme.border};border-radius:16px;
  padding:10px;z-index:1500;box-shadow:0 8px 30px rgba(0,0,0,0.15);min-width:160px}
.theme-panel.open{display:block}
.theme-option{display:flex;align-items:center;gap:8px;padding:8px 12px;
  border-radius:10px;cursor:pointer;font-size:13px;color:${theme.text};transition:0.15s}
.theme-option:hover,.theme-option.active{background:${theme.primary}22;color:${theme.primary};font-weight:600}

@media(max-width:550px){
  .grid{grid-template-columns:repeat(auto-fill,minmax(140px,1fr))}
  .header h1{font-size:17px}
  .header-right .user-badge{display:none}
}
</style>
</head>
<body>
<div class="container">

  <!-- HEADER -->
  <div class="header">
    <div class="header-left">
      <h1>☁️ Finix Cloud</h1>
    </div>
    <div class="header-right">
      <span class="user-badge">👤 ${escapeHtml(user.username)}</span>
      <button class="theme-btn" onclick="toggleTheme()" title="Ganti tema">🎨</button>
      <a href="/logout" class="btn-logout">Keluar</a>
    </div>
  </div>

  <!-- THEME PANEL -->
  <div class="theme-panel" id="themePanel">
    ${Object.entries(themes).map(([k, v]) => `
    <div class="theme-option ${(req.session.theme || 'default') === k ? 'active' : ''}" onclick="setTheme('${k}')">
      ${v.name}
    </div>`).join('')}
  </div>

  <!-- STORAGE -->
  <div class="storage-card">
    <div class="storage-info">
      <div class="storage-text">💾 ${formatSize(storageUsed)} / ${formatSize(user.quota)}</div>
      <div class="storage-bar"><div class="storage-fill"></div></div>
      <div class="storage-pct">${storagePercent.toFixed(1)}% terpakai</div>
    </div>
  </div>

  <!-- BREADCRUMB -->
  <div class="breadcrumb">
    ${breadcrumbs.map((b, i) =>
      i === breadcrumbs.length - 1
        ? `<span>📁 ${escapeHtml(b.name)}</span>`
        : `<a href="/?path=${encodeURIComponent(b.path)}">📁 ${escapeHtml(b.name)}</a> <span style="opacity:0.4">›</span> `
    ).join('')}
  </div>

  <!-- TOOLBAR -->
  <div class="toolbar">
    <div class="upload-area" id="uploadArea">
      <label class="file-input-label" for="fileInputHidden" id="fileLabel">
        📎 Pilih file untuk diupload...
      </label>
      <input type="file" id="fileInputHidden" multiple>
      <button class="btn btn-sm" id="uploadBtn" onclick="startUpload()" disabled>📤 Upload</button>
    </div>
    <button class="btn btn-folder" onclick="openModal('createFolder')">📁 Folder Baru</button>
  </div>

  <!-- UPLOAD PROGRESS -->
  <div class="upload-progress" id="uploadProgress">
    <div class="progress-text" id="progressText">Mengupload...</div>
    <div class="progress-bar"><div class="progress-fill" id="progressFill"></div></div>
  </div>

  <!-- STATS -->
  <div class="stats">${totalItems} item${totalItems !== 1 ? 's' : ''} · ${folders.length} folder, ${files.length} file</div>

  <!-- GRID -->
  <div class="grid" id="fileGrid">
    ${currentPath ? `
    <div class="item item-back" onclick="window.location.href='/?path=${encodeURIComponent(parentPath)}'">
      <div class="icon">⬆️</div>
      <div class="name">Ke atas</div>
      <div class="size">Folder induk</div>
    </div>` : ''}

    ${folders.map(f => `
    <div class="item" onclick="window.location.href='/?path=${encodeURIComponent(f.path)}'">
      <div class="menu-dots" onclick="event.stopPropagation();showMenu(event,'folder','${escapeHtml(f.path)}','${escapeHtml(f.name)}')">⋮</div>
      <div class="icon">📁</div>
      <div class="name">${escapeHtml(f.name)}</div>
      <div class="size">Folder</div>
    </div>`).join('')}

    ${files.map(f => `
    <div class="item" onclick="${isPreviewable(f.name) ? `openPreview('${escapeHtml(f.path)}')` : `window.location.href='/download?path=${encodeURIComponent(f.path)}'`}">
      <div class="menu-dots" onclick="event.stopPropagation();showMenu(event,'file','${escapeHtml(f.path)}','${escapeHtml(f.name)}')">⋮</div>
      <div class="icon">${getFileIcon(f.name)}</div>
      <div class="name">${escapeHtml(f.name)}</div>
      <div class="size">${formatSize(f.size)}</div>
    </div>`).join('')}

    ${totalItems === 0 && !currentPath ? `
    <div class="empty-state" style="grid-column:1/-1">
      <div class="empty-icon">☁️</div>
      <h3>Storage masih kosong</h3>
      <p>Upload file atau buat folder baru untuk memulai</p>
    </div>` : ''}
  </div>

  <div class="footer">☁️ Finix Cloud v4.6 · Secure & Private · ${escapeHtml(user.username)}</div>
</div>

<!-- ACTION MENU -->
<div id="actionMenu" class="action-menu">
  <a id="menuPreview" href="#"><span>👁️</span> Preview</a>
  <a id="menuDownload" href="#"><span>⬇️</span> Download</a>
  <div class="divider"></div>
  <a id="menuRename" href="#"><span>✏️</span> Rename</a>
  <a id="menuMove" href="#"><span>📂</span> Pindahkan</a>
  <div class="divider"></div>
  <a id="menuDelete" href="#" class="danger"><span>🗑️</span> Hapus</a>
</div>

<!-- MODAL CREATE FOLDER -->
<div class="modal-overlay" id="modal-createFolder">
  <div class="modal">
    <h3>📁 Folder Baru</h3>
    <input type="text" id="folderNameInput" placeholder="Nama folder..." maxlength="100">
    <div class="modal-actions">
      <button class="btn btn-sm btn-cancel btn" onclick="closeModal('createFolder')">Batal</button>
      <button class="btn btn-sm" onclick="createFolder()">Buat</button>
    </div>
  </div>
</div>

<!-- MODAL RENAME -->
<div class="modal-overlay" id="modal-rename">
  <div class="modal">
    <h3>✏️ Rename</h3>
    <input type="text" id="renameInput" placeholder="Nama baru..." maxlength="100">
    <div class="modal-actions">
      <button class="btn btn-sm btn-cancel btn" onclick="closeModal('rename')">Batal</button>
      <button class="btn btn-sm" onclick="doRename()">Simpan</button>
    </div>
  </div>
</div>

<!-- MODAL MOVE -->
<div class="modal-overlay" id="modal-move">
  <div class="modal" style="max-width:480px">
    <h3>📂 Pindahkan ke Folder</h3>
    <div id="moveBreadcrumb" style="font-size:12px;color:${theme.textSecondary};margin-bottom:10px;word-break:break-word"></div>
    <div id="moveFolderList" style="max-height:260px;overflow-y:auto;border:1px solid ${theme.border};border-radius:12px;background:${theme.bg}"></div>
    <div style="margin-top:12px;font-size:13px;color:${theme.textSecondary}">
      Tujuan: <strong id="moveDestLabel" style="color:${theme.accent}">—</strong>
    </div>
    <div class="modal-actions">
      <button class="btn btn-sm btn-cancel btn" onclick="closeModal('move')">Batal</button>
      <button class="btn btn-sm" id="moveConfirmBtn" onclick="doMove()" disabled>Pindahkan</button>
    </div>
  </div>
</div>

<!-- PREVIEW MODAL -->
<div class="preview-overlay" id="previewOverlay">
  <div class="preview-header">
    <span class="preview-title" id="previewTitle">Preview</span>
    <button class="preview-close" onclick="closePreview()">✕</button>
  </div>
  <div class="preview-body" id="previewBody">
    <div class="preview-loader"><div class="spinner"></div> Memuat...</div>
  </div>
  <div class="preview-footer" id="previewFooter"></div>
</div>

<script>
const CURRENT_PATH = ${JSON.stringify(currentPath)};
let currentMenuPath = '', currentMenuType = '', currentMenuName = '';

// ====== UPLOAD ======
const fileInput = document.getElementById('fileInputHidden');
const uploadBtn = document.getElementById('uploadBtn');
const fileLabel = document.getElementById('fileLabel');
const uploadArea = document.getElementById('uploadArea');

fileInput.addEventListener('change', () => {
  if (fileInput.files.length > 0) {
    fileLabel.textContent = fileInput.files.length === 1
      ? fileInput.files[0].name
      : fileInput.files.length + ' file dipilih';
    uploadBtn.disabled = false;
  } else {
    fileLabel.textContent = '📎 Pilih file untuk diupload...';
    uploadBtn.disabled = true;
  }
});

// Drag and drop
uploadArea.addEventListener('dragover', e => { e.preventDefault(); uploadArea.classList.add('dragover'); });
uploadArea.addEventListener('dragleave', () => uploadArea.classList.remove('dragover'));
uploadArea.addEventListener('drop', e => {
  e.preventDefault();
  uploadArea.classList.remove('dragover');
  if (e.dataTransfer.files.length > 0) {
    fileInput.files = e.dataTransfer.files;
    fileInput.dispatchEvent(new Event('change'));
  }
});

function startUpload() {
  if (!fileInput.files.length) return;
  const files = Array.from(fileInput.files);
  uploadBtn.disabled = true;
  uploadBtn.textContent = 'Mengupload...';
  
  const progress = document.getElementById('uploadProgress');
  const progressFill = document.getElementById('progressFill');
  const progressText = document.getElementById('progressText');
  progress.classList.add('active');

  let uploaded = 0;
  const total = files.length;

  function uploadNext(idx) {
    if (idx >= files.length) {
      progressText.textContent = '✅ Selesai! Memuat ulang...';
      progressFill.style.width = '100%';
      setTimeout(() => window.location.reload(), 800);
      return;
    }
    const file = files[idx];
    progressText.textContent = 'Mengupload ' + (idx+1) + '/' + total + ': ' + file.name;

    const formData = new FormData();
    formData.append('file', file);
    formData.append('currentPath', CURRENT_PATH);

    const xhr = new XMLHttpRequest();
    xhr.upload.addEventListener('progress', e => {
      if (e.lengthComputable) {
        const pct = ((idx + e.loaded/e.total) / total * 100).toFixed(1);
        progressFill.style.width = pct + '%';
      }
    });
    xhr.addEventListener('load', () => { uploaded++; uploadNext(idx + 1); });
    xhr.addEventListener('error', () => {
      progressText.textContent = '❌ Gagal upload: ' + file.name;
      uploadBtn.disabled = false;
      uploadBtn.textContent = '📤 Upload';
    });
    xhr.open('POST', '/upload');
    xhr.send(formData);
  }

  uploadNext(0);
}

// ====== ACTION MENU ======
function showMenu(event, type, filePath, fileName) {
  event.stopPropagation();
  currentMenuPath = filePath;
  currentMenuType = type;
  currentMenuName = fileName;

  const menu = document.getElementById('actionMenu');
  const menuPreview = document.getElementById('menuPreview');
  const menuDownload = document.getElementById('menuDownload');
  const menuRename = document.getElementById('menuRename');
  const menuMove = document.getElementById('menuMove');
  const menuDelete = document.getElementById('menuDelete');

  if (type === 'folder') {
    menuPreview.style.display = 'none';
    menuDownload.style.display = 'none';
  } else {
    menuPreview.style.display = 'flex';
    menuDownload.style.display = 'flex';
    menuPreview.onclick = e => { e.preventDefault(); openPreview(currentMenuPath); closeMenu(); };
    menuDownload.onclick = e => { e.preventDefault(); window.location.href = '/download?path=' + encodeURIComponent(currentMenuPath); closeMenu(); };
  }

  menuRename.onclick = e => { e.preventDefault(); openRenameModal(currentMenuPath, currentMenuName); closeMenu(); };
  menuMove.onclick = e => { e.preventDefault(); openMoveModal(currentMenuPath, currentMenuName); closeMenu(); };
  menuDelete.onclick = e => { e.preventDefault(); deleteItem(currentMenuPath); closeMenu(); };

  const rect = event.currentTarget.getBoundingClientRect();
  menu.style.display = 'block';
  let left = rect.right - 150;
  let top = rect.bottom + 6;
  if (left < 8) left = 8;
  if (left + 160 > window.innerWidth) left = window.innerWidth - 165;
  if (top + 160 > window.innerHeight) top = rect.top - 165;
  menu.style.left = left + 'px';
  menu.style.top = top + 'px';

  setTimeout(() => document.addEventListener('click', closeMenuOnClick), 100);
}

function closeMenu() {
  document.getElementById('actionMenu').style.display = 'none';
  document.removeEventListener('click', closeMenuOnClick);
}
function closeMenuOnClick(e) {
  if (!document.getElementById('actionMenu').contains(e.target)) closeMenu();
}

// ====== MODALS ======
function openModal(id) {
  document.getElementById('modal-' + id).classList.add('active');
  const inp = document.querySelector('#modal-' + id + ' input');
  if (inp) { inp.value = ''; setTimeout(() => inp.focus(), 100); }
}
function closeModal(id) {
  document.getElementById('modal-' + id).classList.remove('active');
}
document.querySelectorAll('.modal-overlay').forEach(m => {
  m.addEventListener('click', e => { if (e.target === m) m.classList.remove('active'); });
});

// ====== CREATE FOLDER ======
function createFolder() {
  const name = document.getElementById('folderNameInput').value.trim();
  if (!name) return;
  // Validasi nama folder
  if (/[<>:"/\\|?*\x00]/.test(name)) { alert('Nama folder mengandung karakter tidak valid'); return; }
  
  fetch('/create-folder', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'folderName=' + encodeURIComponent(name) + '&currentPath=' + encodeURIComponent(CURRENT_PATH)
  }).then(r => r.json()).then(d => {
    if (d.success) window.location.reload();
    else alert('Error: ' + d.error);
  });
  closeModal('createFolder');
}
document.getElementById('folderNameInput').addEventListener('keydown', e => {
  if (e.key === 'Enter') createFolder();
  if (e.key === 'Escape') closeModal('createFolder');
});

// ====== RENAME ======
let renamePath = '';
function openRenameModal(filePath, fileName) {
  renamePath = filePath;
  document.getElementById('renameInput').value = fileName;
  openModal('rename');
}
function doRename() {
  const newName = document.getElementById('renameInput').value.trim();
  if (!newName) return;
  if (/[<>:"/\\|?*\x00]/.test(newName)) { alert('Nama mengandung karakter tidak valid'); return; }
  
  fetch('/rename', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'oldPath=' + encodeURIComponent(renamePath) + '&newName=' + encodeURIComponent(newName)
  }).then(r => r.json()).then(d => {
    if (d.success) window.location.reload();
    else alert('Gagal rename: ' + d.error);
  });
  closeModal('rename');
}
document.getElementById('renameInput').addEventListener('keydown', e => {
  if (e.key === 'Enter') doRename();
  if (e.key === 'Escape') closeModal('rename');
});

// ====== DELETE ======
async function deleteItem(filePath) {
  if (!confirm('Hapus item ini? Tindakan tidak bisa dibatalkan.')) return;
  const r = await fetch('/delete', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'path=' + encodeURIComponent(filePath)
  });
  const d = await r.json();
  if (d.success) window.location.reload();
  else alert('Gagal hapus: ' + d.error);
}

// ====== MOVE ======
let moveSrcPath = '', moveDestPath = '', moveBrowsePath = '';

async function openMoveModal(srcPath, srcName) {
  moveSrcPath = srcPath;
  moveDestPath = '';
  // Mulai browse dari root
  moveBrowsePath = '';
  document.getElementById('moveDestLabel').textContent = '—';
  document.getElementById('moveConfirmBtn').disabled = true;
  openModal('move');
  await loadMoveFolders('');
}

async function loadMoveFolders(browsePath) {
  moveBrowsePath = browsePath;
  const list = document.getElementById('moveFolderList');
  const bc = document.getElementById('moveBreadcrumb');
  list.innerHTML = '<div class="move-empty">⏳ Memuat...</div>';

  // Update breadcrumb
  const parts = browsePath ? browsePath.split('/') : [];
  let bcHtml = '<span style="cursor:pointer;color:var(--accent)" onclick="loadMoveFolders(\\'\\')">🏠 Root</span>';
  let built = '';
  parts.forEach((p, i) => {
    built = built ? built + '/' + p : p;
    const cap = built;
    bcHtml += ' <span style="opacity:0.4">›</span> <span style="cursor:pointer;color:var(--accent)" onclick="loadMoveFolders(\\''+cap+'\\')">'+p+'</span>';
  });
  bc.innerHTML = '📍 ' + bcHtml;

  try {
    const r = await fetch('/list-folders?path=' + encodeURIComponent(browsePath));
    const d = await r.json();
    if (d.error) { list.innerHTML = '<div class="move-empty">⚠️ ' + d.error + '</div>'; return; }

    let html = '';

    // Tombol naik ke folder induk
    if (browsePath) {
      const up = browsePath.split('/').slice(0,-1).join('/');
      html += \`<div class="move-folder-item up-item" onclick="loadMoveFolders('\${up}')">
        <span>⬆️</span> <span>Ke atas (../)</span>
      </div>\`;
    }

    // Opsi "pindahkan ke sini" untuk folder current
    const isRoot = browsePath === '';
    const isSrcParent = moveSrcPath.includes('/') 
      ? moveSrcPath.split('/').slice(0,-1).join('/') === browsePath
      : browsePath === '';
    const destLabel = isRoot ? 'Root' : browsePath.split('/').pop();

    html += \`<div class="move-folder-item current-dir \${moveDestPath === browsePath ? 'selected' : ''}" 
      onclick="selectMoveDest('\${browsePath}', '\${destLabel}')">
      <span>📍</span> <span>Taruh di sini: <strong>\${isRoot ? '🏠 Root' : '📁 '+destLabel}</strong></span>
    </div>\`;

    // Daftar subfolder
    if (d.folders.length === 0 && !browsePath) {
      html += '<div class="move-empty">Belum ada subfolder</div>';
    }
    d.folders.forEach(f => {
      // Jangan tampilkan folder yang sedang dipindahkan (kalau tipe folder)
      if (f.path === moveSrcPath) return;
      html += \`<div class="move-folder-item" onclick="loadMoveFolders('\${f.path}')">
        <span>📁</span> <span>\${f.name}</span> <span style="margin-left:auto;opacity:0.4;font-size:11px">›</span>
      </div>\`;
    });

    list.innerHTML = html || '<div class="move-empty">Folder kosong</div>';
  } catch(e) {
    list.innerHTML = '<div class="move-empty">⚠️ Gagal memuat folder</div>';
  }
}

function selectMoveDest(destPath, destLabel) {
  moveDestPath = destPath;
  // Update UI selected
  document.querySelectorAll('.move-folder-item').forEach(el => el.classList.remove('selected'));
  event.currentTarget.classList.add('selected');
  document.getElementById('moveDestLabel').textContent = destPath === '' ? '🏠 Root' : '📁 ' + destPath;
  document.getElementById('moveConfirmBtn').disabled = false;
}

async function doMove() {
  if (!moveSrcPath) return;
  const r = await fetch('/move', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'srcPath=' + encodeURIComponent(moveSrcPath) + '&destDir=' + encodeURIComponent(moveDestPath)
  });
  const d = await r.json();
  if (d.success) {
    closeModal('move');
    window.location.reload();
  } else {
    alert('Gagal memindahkan: ' + d.error);
  }
}

// ====== PREVIEW ======
async function openPreview(filePath) {
  const overlay = document.getElementById('previewOverlay');
  const body = document.getElementById('previewBody');
  const title = document.getElementById('previewTitle');
  const footer = document.getElementById('previewFooter');

  overlay.classList.add('active');
  body.innerHTML = '<div class="preview-loader"><div class="spinner"></div> Memuat...</div>';
  title.textContent = filePath.split('/').pop();
  footer.textContent = '';

  try {
    const r = await fetch('/preview-data?path=' + encodeURIComponent(filePath));
    const d = await r.json();

    if (d.error) {
      body.innerHTML = '<div class="no-preview">⚠️ ' + d.error + '</div>';
      return;
    }

    title.textContent = d.filename || filePath.split('/').pop();

    if (d.type === 'image') {
      body.innerHTML = '<img src="' + d.url + '" alt="' + d.filename + '" loading="lazy">';
    }
    else if (d.type === 'video') {
      // Improved video player dengan controls lengkap
      body.innerHTML = \`<video controls autoplay playsinline preload="metadata"
        style="max-width:100%;max-height:80vh;border-radius:12px;background:#000;outline:none"
        controlslist="nodownload" oncontextmenu="return false;">
        <source src="\${d.url}" type="\${d.mime}">
        <p style="color:#888">Browser tidak mendukung format video ini.</p>
      </video>\`;
      // Fokus ke video agar keyboard controls (spasi, arrows) berfungsi
      setTimeout(() => body.querySelector('video')?.focus(), 100);
    }
    else if (d.type === 'audio') {
      body.innerHTML = \`<div style="padding:40px;text-align:center;width:100%">
        <div style="font-size:80px;margin-bottom:20px">🎵</div>
        <div style="color:white;font-size:16px;font-weight:600;margin-bottom:20px">\${d.filename}</div>
        <audio controls autoplay style="width:100%;max-width:500px">
          <source src="\${d.url}" type="\${d.mime}">
          Browser tidak mendukung audio ini.
        </audio>
      </div>\`;
    }
    else if (d.type === 'pdf') {
      body.innerHTML = \`<iframe src="\${d.url}#toolbar=1&navpanes=0" style="width:100%;height:78vh;border:none;border-radius:8px"></iframe>\`;
    }
    else if (d.type === 'text') {
      const escaped = d.content.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
      body.innerHTML = '<pre>' + escaped + '</pre>';
      footer.textContent = d.content.split('\\n').length + ' baris · ' + d.content.length + ' karakter';
    }
    else {
      body.innerHTML = '<div class="no-preview">❌ Format tidak dapat dipreview</div>';
    }
  } catch(e) {
    body.innerHTML = '<div class="no-preview">⚠️ Gagal memuat preview</div>';
  }
}

function closePreview() {
  const overlay = document.getElementById('previewOverlay');
  overlay.classList.remove('active');
  // Hentikan video/audio saat ditutup
  overlay.querySelectorAll('video,audio').forEach(m => { m.pause(); m.src = ''; });
  document.getElementById('previewBody').innerHTML = '';
}

// Tutup preview dengan ESC
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') {
    if (document.getElementById('previewOverlay').classList.contains('active')) closePreview();
    else if (document.getElementById('modal-createFolder').classList.contains('active')) closeModal('createFolder');
    else if (document.getElementById('modal-rename').classList.contains('active')) closeModal('rename');
    else if (document.getElementById('modal-move').classList.contains('active')) closeModal('move');
  }
});

// Klik overlay untuk tutup preview
document.getElementById('previewOverlay').addEventListener('click', e => {
  if (e.target === document.getElementById('previewOverlay')) closePreview();
});

// ====== THEME ======
function toggleTheme() {
  document.getElementById('themePanel').classList.toggle('open');
  document.addEventListener('click', closeThemeOnClick, { once: true });
}
function closeThemeOnClick(e) {
  if (!document.getElementById('themePanel').contains(e.target) &&
      e.target.className !== 'theme-btn') {
    document.getElementById('themePanel').classList.remove('open');
  }
}
function setTheme(name) {
  fetch('/set-theme', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'theme=' + encodeURIComponent(name)
  }).then(() => window.location.reload());
}
</script>
</body>
</html>`);
});

// ======================== API ROUTES ========================
app.post('/create-folder', requireLogin, (req, res) => {
  const { folderName, currentPath } = req.body;
  if (!folderName || !folderName.trim()) return res.json({ error: 'Nama folder diperlukan' });
  if (/[<>:"/\\|?*\x00]/.test(folderName)) return res.json({ error: 'Nama mengandung karakter tidak valid' });

  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetDir = safePath(userDir, currentPath);
    if (!targetDir) return res.json({ error: 'Path tidak valid' });
    const newFolder = safePath(targetDir, folderName.trim());
    if (!newFolder) return res.json({ error: 'Nama folder tidak valid' });
    if (!fs.existsSync(newFolder)) fs.mkdirSync(newFolder, { recursive: true });
    res.json({ success: true });
  } catch(err) { res.json({ error: err.message }); }
});

// FIX: Upload multi-file support + path protection
app.post('/upload', requireLogin, upload.single('file'), (req, res) => {
  // Cek quota setelah upload
  const user = users[req.session.userId];
  const userDir = path.join(STORAGE_DIR, req.session.userId);
  const storageUsed = getFolderSize(userDir);
  if (storageUsed > user.quota) {
    // Hapus file yang baru diupload jika quota terlampaui
    if (req.file) {
      try { fs.unlinkSync(req.file.path); } catch(e) {}
    }
    return res.redirect(`/?path=${encodeURIComponent(req.body.currentPath || '')}&err=quota`);
  }
  res.redirect(`/?path=${encodeURIComponent(req.body.currentPath || '')}`);
}, (err, req, res, next) => {
  // Error handler multer
  if (err) return res.redirect(`/?path=${encodeURIComponent(req.body?.currentPath || '')}&err=upload`);
  next();
});

app.get('/download', requireLogin, (req, res) => {
  const filePath = req.query.path;
  if (!filePath) return res.status(404).send(errorPage('404', 'File tidak ditemukan'));
  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetFile = safePath(userDir, filePath);
    if (!targetFile) return res.status(403).send(errorPage('403', 'Akses ditolak'));
    if (fs.existsSync(targetFile) && fs.statSync(targetFile).isFile()) {
      res.download(targetFile);
    } else res.status(404).send(errorPage('404', 'File tidak ditemukan'));
  } catch(err) { res.status(500).send(errorPage('500', 'Server error')); }
});

app.get('/preview-data', requireLogin, (req, res) => {
  const filePath = req.query.path;
  if (!filePath) return res.json({ error: 'Tidak ada file' });
  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetFile = safePath(userDir, filePath);
    if (!targetFile) return res.json({ error: 'Path tidak valid' });
    if (!fs.existsSync(targetFile) || !fs.statSync(targetFile).isFile()) {
      return res.json({ error: 'File tidak ditemukan' });
    }

    const filename = path.basename(targetFile);
    const streamUrl = `/stream?path=${encodeURIComponent(filePath)}`;

    if (isImageFile(filename)) return res.json({ type: 'image', url: streamUrl, filename, mime: getMimeType(filename) });
    if (isVideoFile(filename)) return res.json({ type: 'video', url: streamUrl, filename, mime: getMimeType(filename) });
    if (isAudioFile(filename)) return res.json({ type: 'audio', url: streamUrl, filename, mime: getMimeType(filename) });
    if (path.extname(filename).toLowerCase() === '.pdf') return res.json({ type: 'pdf', url: streamUrl, filename });
    if (isTextFile(filename)) {
      const stat = fs.statSync(targetFile);
      if (stat.size > 1024 * 1024) return res.json({ error: 'File terlalu besar untuk dipreview (>1MB)' });
      const content = fs.readFileSync(targetFile, 'utf8');
      return res.json({ type: 'text', content, filename });
    }
    return res.json({ error: 'Format tidak didukung untuk preview' });
  } catch(err) { res.json({ error: 'Gagal membaca file' }); }
});

// FIX: Stream dengan Range support (penting untuk video seeking)
app.get('/stream', requireLogin, (req, res) => {
  const filePath = req.query.path;
  if (!filePath) return res.status(404).send('Not found');
  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetFile = safePath(userDir, filePath);
    if (!targetFile) return res.status(403).send('Forbidden');
    if (!fs.existsSync(targetFile) || !fs.statSync(targetFile).isFile()) {
      return res.status(404).send('File not found');
    }

    const stat = fs.statSync(targetFile);
    const mime = getMimeType(targetFile);
    const fileSize = stat.size;
    const range = req.headers.range;

    if (range) {
      // Partial content untuk video seeking
      const parts = range.replace(/bytes=/, '').split('-');
      const start = parseInt(parts[0], 10);
      const end = parts[1] ? parseInt(parts[1], 10) : fileSize - 1;
      const chunkSize = (end - start) + 1;

      res.writeHead(206, {
        'Content-Range': `bytes ${start}-${end}/${fileSize}`,
        'Accept-Ranges': 'bytes',
        'Content-Length': chunkSize,
        'Content-Type': mime,
        'Cache-Control': 'no-store'
      });
      fs.createReadStream(targetFile, { start, end }).pipe(res);
    } else {
      res.writeHead(200, {
        'Content-Length': fileSize,
        'Content-Type': mime,
        'Accept-Ranges': 'bytes',
        'Cache-Control': 'no-store'
      });
      fs.createReadStream(targetFile).pipe(res);
    }
  } catch(err) { res.status(500).send('Error'); }
});

app.post('/delete', requireLogin, (req, res) => {
  const { path: deletePath } = req.body;
  if (!deletePath) return res.json({ error: 'Path tidak ada' });
  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetPath = safePath(userDir, deletePath);
    if (!targetPath) return res.json({ error: 'Path tidak valid' });
    if (fs.existsSync(targetPath)) {
      const stat = fs.statSync(targetPath);
      if (stat.isDirectory()) fs.rmSync(targetPath, { recursive: true, force: true });
      else fs.unlinkSync(targetPath);
      res.json({ success: true });
    } else res.json({ error: 'Item tidak ditemukan' });
  } catch(err) { res.json({ error: err.message }); }
});

// NEW: List folders untuk move modal
app.get('/list-folders', requireLogin, (req, res) => {
  const browsePath = req.query.path || '';
  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetDir = safePath(userDir, browsePath);
    if (!targetDir) return res.json({ error: 'Path tidak valid' });
    if (!fs.existsSync(targetDir)) return res.json({ error: 'Folder tidak ditemukan' });

    const items = fs.readdirSync(targetDir);
    const folders = [];
    for (const item of items) {
      const itemPath = path.join(targetDir, item);
      try {
        const stat = fs.statSync(itemPath);
        if (stat.isDirectory()) {
          const relPath = browsePath ? `${browsePath}/${item}` : item;
          folders.push({ name: item, path: relPath });
        }
      } catch(e) {}
    }
    folders.sort((a, b) => a.name.localeCompare(b.name));
    res.json({ folders });
  } catch(err) {
    res.json({ error: err.message });
  }
});

// NEW: Move file/folder
app.post('/move', requireLogin, (req, res) => {
  const { srcPath, destDir } = req.body;
  if (srcPath === undefined) return res.json({ error: 'srcPath diperlukan' });
  // destDir boleh '' (root)

  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const srcFull = safePath(userDir, srcPath);
    if (!srcFull) return res.json({ error: 'Path sumber tidak valid' });
    if (!fs.existsSync(srcFull)) return res.json({ error: 'File/folder sumber tidak ditemukan' });

    const destDirFull = safePath(userDir, destDir || '');
    if (!destDirFull) return res.json({ error: 'Folder tujuan tidak valid' });
    if (!fs.existsSync(destDirFull)) return res.json({ error: 'Folder tujuan tidak ditemukan' });

    const srcName = path.basename(srcFull);
    const destFull = path.join(destDirFull, srcName);

    // Jangan pindah ke diri sendiri
    if (srcFull === destFull) return res.json({ error: 'Sumber dan tujuan sama' });

    // Jangan pindah folder ke dalam dirinya sendiri
    if (fs.statSync(srcFull).isDirectory()) {
      if (destFull.startsWith(srcFull + path.sep) || destFull === srcFull) {
        return res.json({ error: 'Tidak bisa memindahkan folder ke dalam dirinya sendiri' });
      }
    }

    // Cek konflik nama di tujuan
    if (fs.existsSync(destFull)) return res.json({ error: `"${srcName}" sudah ada di folder tujuan` });

    fs.moveSync(srcFull, destFull);
    res.json({ success: true });
  } catch(err) {
    res.json({ error: err.message });
  }
});

// NEW: Rename endpoint
app.post('/rename', requireLogin, (req, res) => {
  const { oldPath, newName } = req.body;
  if (!oldPath || !newName) return res.json({ error: 'Parameter kurang' });
  if (/[<>:"/\\|?*\x00]/.test(newName)) return res.json({ error: 'Nama mengandung karakter tidak valid' });

  try {
    const userDir = path.join(STORAGE_DIR, req.session.userId);
    const targetPath = safePath(userDir, oldPath);
    if (!targetPath) return res.json({ error: 'Path tidak valid' });
    if (!fs.existsSync(targetPath)) return res.json({ error: 'File tidak ditemukan' });

    const parentDir = path.dirname(targetPath);
    const newPath = path.join(parentDir, newName.trim());

    // Pastikan newPath masih di dalam userDir
    if (!newPath.startsWith(path.resolve(userDir))) return res.json({ error: 'Path tidak valid' });

    if (fs.existsSync(newPath)) return res.json({ error: 'Nama sudah digunakan' });
    fs.renameSync(targetPath, newPath);
    res.json({ success: true });
  } catch(err) { res.json({ error: err.message }); }
});

// NEW: Set theme
app.post('/set-theme', requireLogin, (req, res) => {
  const { theme } = req.body;
  if (themes[theme]) {
    req.session.theme = theme;
    req.session.save(() => res.json({ success: true }));
  } else res.json({ error: 'Tema tidak valid' });
});

app.get('/logout', (req, res) => {
  req.session.destroy(() => {
    res.clearCookie('finix.sid');
    res.redirect('/login');
  });
});

// ======================== ERROR HANDLER ========================
app.use((req, res) => res.status(404).send(errorPage('404 - Tidak Ditemukan', 'Halaman yang kamu cari tidak ada.')));
app.use((err, req, res, next) => {
  console.error(err);
  res.status(500).send(errorPage('500 - Server Error', 'Terjadi kesalahan pada server.'));
});

// ======================== START SERVER ========================
app.listen(PORT, '0.0.0.0', () => {
  console.log('');
  console.log('========================================');
  console.log(' FINIX CLOUD v4.6 — ENHANCED EDITION');
  console.log('========================================');
  console.log(` URL    : http://localhost:${PORT}`);
  if (Object.keys(users).length === 0) {
    console.log(' STATUS : ⚠️  Belum ada akun — buka /setup');
  } else {
    console.log(` STATUS : ✅ ${Object.keys(users).length} akun terdaftar`);
  }
  console.log(` Data   : ${DATA_DIR}`);
  console.log(` Storage: ${STORAGE_DIR}`);
  console.log('========================================');
  console.log('');
});
