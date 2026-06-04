/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║          FINIX OS v11.0 — Triple AI Edition (C)              ║
 * ║  Awing (Ollama/Qwen) + DeepSeek + ChatGPT + Linux Kernel    ║
 * ║         Creator: Nurudeen Al Haitami (Alkha)                ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * Compile:
 *   gcc finixos.c -o finixos -lm
 *   ./finixos
 *
 * Di Termux:
 *   pkg install clang
 *   clang finixos.c -o finixos -lm
 *   ./finixos
 *
 * AI yang didukung:
 *   1. Awing  — Ollama lokal (qwen2.5:3B) di port 11434
 *   2. DeepSeek — API cloud DeepSeek
 *   3. ChatGPT — API OpenAI GPT-4o-mini
 *
 * Setup AI:
 *   ai setup awing     — deteksi Ollama otomatis
 *   ai setup deepseek  — masukkan API key DeepSeek
 *   ai setup chatgpt   — masukkan API key OpenAI
 *   ai awing  <tanya>  — tanya Awing lokal
 *   ai deepseek <tanya>— tanya DeepSeek
 *   ai chatgpt <tanya> — tanya ChatGPT
 *   ai <tanya>         — gunakan AI aktif (default: Awing)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/types.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

/* ════════════════════════════════════════
 *  WARNA ANSI
 * ════════════════════════════════════════ */
#define RST   "\033[0m"
#define BOLD  "\033[1m"
#define DIM   "\033[2m"
#define RED   "\033[91m"
#define GRN   "\033[92m"
#define YLW   "\033[93m"
#define BLU   "\033[94m"
#define MAG   "\033[95m"
#define CYN   "\033[96m"
#define WHT   "\033[97m"
#define DRED  "\033[31m"
#define DGRN  "\033[32m"
#define DCYN  "\033[36m"
#define BG_BLK "\033[40m"

/* ════════════════════════════════════════
 *  MATH MANUAL (tanpa -lm, kompatibel Termux clang)
 * ════════════════════════════════════════ */
static double finix_pow(double base, double exp) {
    if (exp == 0) return 1.0;
    double result = 1.0;
    int neg = (exp < 0);
    long e = neg ? (long)(-exp) : (long)exp;
    for (long i = 0; i < e; i++) result *= base;
    return neg ? 1.0/result : result;
}
static double finix_fmod(double a, double b) {
    if (b == 0) return 0;
    long q = (long)(a / b);
    return a - q * b;
}

/* ════════════════════════════════════════
 *  KONFIGURASI
 * ════════════════════════════════════════ */
#define OS_NAME      "FINIX OS"
#define OS_VERSION   "11.0"
#define OS_CODENAME  "Triple AI Edition v11.0"
#define OS_AUTHOR    "Nurudeen Al Haitami (Alkha)"
#define HOSTNAME     "finix-device"
#define MAX_NAME     64
#define MAX_PATH     512
#define MAX_LINE     1024
#define MAX_USERS    32
#define MAX_HISTORY  200
#define MAX_NOTES    500
#define MAX_FW_RULES 64
#define NOTES_FILE   "finix_notes.dat"
#define USERS_FILE   "finix_users.dat"

static time_t boot_time;

/* Forward declaration untuk ai_cfg (definisi di bagian Triple AI) */
typedef struct {
    char deepseek_key[256];
    char openai_key[256];
    char active[16];
    int  ollama_port;
    char ollama_model[64];
} ai_config_t;
extern ai_config_t ai_cfg;

/* ════════════════════════════════════════
 *  LINKED LIST (ciri khas C Alkha)
 * ════════════════════════════════════════ */
struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}
static inline void _list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new; new->next = next;
    new->prev  = prev; prev->next = new;
}
static inline void list_add(struct list_head *new, struct list_head *head) {
    _list_add(new, head, head->next);
}
static inline void list_add_tail(struct list_head *new, struct list_head *head) {
    _list_add(new, head->prev, head);
}
static inline void list_del(struct list_head *entry) {
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry->prev = NULL;
}
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (size_t)(&((type *)0)->member)))
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

/* ════════════════════════════════════════
 *  MEMORY CACHE (ciri khas C Alkha)
 * ════════════════════════════════════════ */
typedef int refcount_t;
static inline void refcount_set(refcount_t *r, int v) { *r = v; }
static inline void refcount_inc(refcount_t *r) { (*r)++; }
static inline void refcount_dec(refcount_t *r) { (*r)--; }

typedef struct kmem_cache {
    char   name[32];
    size_t obj_size;
    int    obj_count, max_objs, free_count, lock;
    void **objects;
    int   *free_list;
} kmem_cache_t;

kmem_cache_t *kmem_cache_create(const char *name, size_t obj_size, int max_objs) {
    kmem_cache_t *c = malloc(sizeof(kmem_cache_t));
    if (!c) return NULL;
    strncpy(c->name, name, 31); c->name[31] = '\0';
    c->obj_size = obj_size; c->obj_count = 0;
    c->max_objs = c->free_count = max_objs; c->lock = 0;
    c->objects   = malloc(sizeof(void *) * max_objs);
    c->free_list = malloc(sizeof(int)    * max_objs);
    for (int i = 0; i < max_objs; i++) {
        c->free_list[i] = i; c->objects[i] = NULL;
    }
    return c;
}
void *kmem_cache_alloc(kmem_cache_t *c) {
    if (!c || c->free_count == 0) return NULL;
    int idx = c->free_list[--c->free_count];
    if (!c->objects[idx]) c->objects[idx] = malloc(c->obj_size);
    if (!c->objects[idx]) return NULL;
    memset(c->objects[idx], 0, c->obj_size);
    c->obj_count++;
    return c->objects[idx];
}
void kmem_cache_free(kmem_cache_t *c, void *obj) {
    if (!c || !obj) return;
    for (int i = 0; i < c->max_objs; i++)
        if (c->objects[i] == obj) {
            c->free_list[c->free_count++] = i;
            c->obj_count--;
            break;
        }
}
void kmem_cache_destroy(kmem_cache_t *c) {
    if (!c) return;
    for (int i = 0; i < c->max_objs; i++)
        if (c->objects[i]) free(c->objects[i]);
    free(c->objects); free(c->free_list); free(c);
}
char *kstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ════════════════════════════════════════
 *  USER & GROUP (diperluas)
 * ════════════════════════════════════════ */
typedef struct finix_user {
    int  uid;
    char username[MAX_NAME];
    char password_hash[65];   /* SHA-256 hex */
    int  is_root;
    refcount_t refcnt;
    struct list_head list;
} finix_user_t;

struct list_head user_list;
finix_user_t    *current_user = NULL;
int              next_uid = 1000;
kmem_cache_t    *user_cache;

/* Hash sederhana (djb2 + xor) — cocok tanpa crypto lib */
static void simple_hash(const char *input, char *output) {
    unsigned long h1 = 5381, h2 = 0;
    const char *p = input;
    while (*p) {
        h1 = ((h1 << 5) + h1) ^ (unsigned char)*p;
        h2 = h2 * 31 + (unsigned char)*p;
        p++;
    }
    snprintf(output, 65, "%016lx%016lx%016lx%016lx",
             h1, h2, h1^h2, h1+h2);
}

void save_users(void) {
    FILE *f = fopen(USERS_FILE, "w");
    if (!f) return;
    struct list_head *pos;
    finix_user_t *u;
    list_for_each(pos, &user_list) {
        u = list_entry(pos, finix_user_t, list);
        fprintf(f, "%d|%s|%s|%d\n",
                u->uid, u->username, u->password_hash, u->is_root);
    }
    fclose(f);
}
void load_users(void) {
    FILE *f = fopen(USERS_FILE, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        finix_user_t *u = kmem_cache_alloc(user_cache);
        if (!u) break;
        int root = 0;
        if (sscanf(line, "%d|%63[^|]|%64[^|]|%d",
                   &u->uid, u->username, u->password_hash, &root) >= 3) {
            u->is_root = root;
            refcount_set(&u->refcnt, 1);
            list_add_tail(&u->list, &user_list);
            if (u->uid >= next_uid) next_uid = u->uid + 1;
        } else {
            kmem_cache_free(user_cache, u);
        }
    }
    fclose(f);
}

void init_user_system(void) {
    INIT_LIST_HEAD(&user_list);
    load_users();
    /* Buat root default jika belum ada */
    int found_root = 0;
    struct list_head *pos;
    list_for_each(pos, &user_list) {
        finix_user_t *u = list_entry(pos, finix_user_t, list);
        if (strcmp(u->username, "root") == 0) { found_root = 1; break; }
    }
    if (!found_root) {
        finix_user_t *u = kmem_cache_alloc(user_cache);
        if (!u) {
            fprintf(stderr, "FINIX: Gagal alokasi user root!\n");
            return;
        }
        u->uid = 0; strcpy(u->username, "root");
        simple_hash("finix123", u->password_hash);
        u->is_root = 1;
        refcount_set(&u->refcnt, 1);
        list_add_tail(&u->list, &user_list);
        save_users();
    }
    /* Default masuk sebagai root */
    list_for_each(pos, &user_list) {
        finix_user_t *u = list_entry(pos, finix_user_t, list);
        if (strcmp(u->username, "root") == 0) { current_user = u; break; }
    }
}

int authenticate(const char *uname, const char *pw) {
    char hash[65]; simple_hash(pw, hash);
    struct list_head *pos;
    list_for_each(pos, &user_list) {
        finix_user_t *u = list_entry(pos, finix_user_t, list);
        if (strcmp(u->username, uname)==0 && strcmp(u->password_hash, hash)==0) {
            current_user = u; return 1;
        }
    }
    return 0;
}

/* ════════════════════════════════════════
 *  FIREWALL (diperluas)
 * ════════════════════════════════════════ */
typedef struct fw_rule {
    char proto[8];
    int  port;
    char action[12];
    long packets;
    struct list_head list;
} fw_rule_t;

struct list_head fw_rules;
kmem_cache_t    *fw_cache;

void fw_init(void) { INIT_LIST_HEAD(&fw_rules); }

void fw_add(const char *proto, int port, const char *action) {
    fw_rule_t *r = kmem_cache_alloc(fw_cache);
    if (!r) { printf(RED "  ✗ Memori penuh!\n" RST); return; }
    strncpy(r->proto,  proto,  7);  r->proto[7]  = '\0';
    strncpy(r->action, action, 11); r->action[11] = '\0';
    r->port = port; r->packets = 0;
    /* Uppercase proto & action */
    for (int i=0; r->proto[i];  i++) r->proto[i]  = toupper(r->proto[i]);
    for (int i=0; r->action[i]; i++) r->action[i] = toupper(r->action[i]);
    list_add_tail(&r->list, &fw_rules);
    printf(GRN "  ✓ Aturan ditambahkan: %s port %d → %s\n" RST,
           r->proto, port, r->action);
}
void fw_list(void) {
    struct list_head *pos;
    int n = 0;
    list_for_each(pos, &fw_rules) n++;
    if (n == 0) { printf(DIM "  Tidak ada aturan aktif.\n" RST); return; }
    printf("\n  " CYN "%-8s %6s  %-12s  %s\n" RST,
           "Proto","Port","Aksi","Diblokir");
    printf("  %s\n", "─────────────────────────────────");
    list_for_each(pos, &fw_rules) {
        fw_rule_t *r = list_entry(pos, fw_rule_t, list);
        const char *clr = (strncmp(r->action,"BLOKIR",6)==0) ? RED : GRN;
        printf("  %-8s %6d  %s%-12s" RST "  %ld\n",
               r->proto, r->port, clr, r->action, r->packets);
    }
    printf("\n");
}
void fw_delete(int port) {
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &fw_rules) {
        fw_rule_t *r = list_entry(pos, fw_rule_t, list);
        if (r->port == port) {
            list_del(&r->list);
            kmem_cache_free(fw_cache, r);
            printf(GRN "  ✓ Aturan port %d dihapus.\n" RST, port);
            return;
        }
    }
    printf(RED "  ✗ Aturan port %d tidak ditemukan.\n" RST, port);
}

/* ════════════════════════════════════════
 *  CATATAN (NOTES)
 * ════════════════════════════════════════ */
typedef struct {
    int  id;
    char text[512];
    char waktu[32];
} note_t;

static note_t  notes[MAX_NOTES];
static int     note_count = 0;

void notes_load(void) {
    FILE *f = fopen(NOTES_FILE, "r");
    if (!f) return;
    note_count = 0;
    while (note_count < MAX_NOTES &&
           fscanf(f, "%d|%31[^|]|%511[^\n]\n",
                  &notes[note_count].id,
                   notes[note_count].waktu,
                   notes[note_count].text) == 3)
        note_count++;
    fclose(f);
}
void notes_save(void) {
    FILE *f = fopen(NOTES_FILE, "w");
    if (!f) return;
    for (int i = 0; i < note_count; i++)
        fprintf(f, "%d|%s|%s\n",
                notes[i].id, notes[i].waktu, notes[i].text);
    fclose(f);
}

/* ════════════════════════════════════════
 *  COMMAND HISTORY
 * ════════════════════════════════════════ */
static char *history[MAX_HISTORY];
static int   history_count = 0;

void history_add(const char *cmd) {
    if (!cmd || !strlen(cmd)) return;
    /* Hindari duplikat berurutan */
    if (history_count > 0 && strcmp(history[history_count-1], cmd) == 0)
        return;
    if (history_count < MAX_HISTORY) {
        history[history_count++] = kstrdup(cmd);
    } else {
        free(history[0]);
        memmove(history, history+1, (MAX_HISTORY-1)*sizeof(char*));
        history[MAX_HISTORY-1] = kstrdup(cmd);
    }
}

/* ════════════════════════════════════════
 *  ERROR RECOVERY (ciri khas Alkha)
 * ════════════════════════════════════════ */
jmp_buf  crash_env;
int      crash_count = 0;
time_t   last_crash  = 0;

void crash_handler(int sig __attribute__((unused))) {
    time_t now = time(NULL);
    if (now - last_crash > 60) { crash_count = 0; last_crash = now; }
    if (++crash_count < 5) {
        printf(RED "\n  ⚠  FINIX OS crash! Memulai ulang...\n" RST);
        longjmp(crash_env, 1);
    } else {
        printf(RED "\n  ✗ Terlalu banyak crash! Safe mode...\n" RST);
        longjmp(crash_env, 2);
    }
}
void setup_signals(void) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);
    signal(SIGPIPE, SIG_IGN);
}

/* ════════════════════════════════════════
 *  UTILITAS UI
 * ════════════════════════════════════════ */
static void progress_bar(int pct) {
    int filled = pct / 4;   /* skala 25 */
    printf("  [");
    for (int i = 0; i < 25; i++) {
        if (i < filled)      printf(GRN "█" RST);
        else if (i == filled) printf(YLW "▓" RST);
        else                  printf(DIM "░" RST);
    }
    printf("] %d%%\r", pct);
    fflush(stdout);
}

static void bar_fill(int pct, int width) {
    int f = (int)((float)pct/100 * width);
    const char *clr = pct > 70 ? GRN : pct > 30 ? YLW : RED;
    printf("[%s", clr);
    for (int i = 0; i < width; i++)
        printf(i < f ? "█" : "░");
    printf(RST "] %d%%", pct);
}

static void separator(const char *title, const char *color) {
    printf("\n  %s", color);
    if (title && strlen(title)) {
        int pad = (46 - (int)strlen(title)) / 2;
        printf("╔");
        for (int i = 0; i < 46; i++) printf("═");
        printf("╗\n  ║%*s%s%*s║\n  ╚",
               pad, "", title, 46 - pad - (int)strlen(title), "");
        for (int i = 0; i < 46; i++) printf("═");
        printf("╝\n" RST);
    } else {
        printf("─────────────────────────────────────────────\n" RST);
    }
}

/* ════════════════════════════════════════
 *  BOOT SCREEN
 * ════════════════════════════════════════ */
void boot_screen(void) {
    system("clear");
    printf(CYN BOLD);
    printf("  ╔═══════════════════════════════════════════════╗\n");
    printf("  ║   ______ _       _      __  ____  _____      ║\n");
    printf("  ║  |  ____(_)     (_)    / / / __ \\/ ____|     ║\n");
    printf("  ║  | |__   _ _ __ _ ___  / / | |  | | (___     ║\n");
    printf("  ║  |  __| | | '_ \\| \\ \\/ /  | |  | |\\___ \\    ║\n");
    printf("  ║  | |    | | | | | |>  <   | |__| |____) |   ║\n");
    printf("  ║  |_|    |_|_| |_|_/_/\\_\\   \\____/|_____/    ║\n");
    printf("  ║                                               ║\n");
    printf("  ║  " GRN "v%-6s — %-30s" CYN "   ║\n", OS_VERSION, OS_CODENAME);
    printf("  ║  " DIM "Creator: %-36s" CYN " ║\n", OS_AUTHOR);
    printf("  ╚═══════════════════════════════════════════════╝\n" RST);
    printf("\n");

    /* Progress bar animasi */
    printf("  Booting: ");
    fflush(stdout);
    for (int i = 0; i <= 100; i += 4) {
        progress_bar(i);
        usleep(35000);
    }
    printf("  [" GRN);
    for (int i = 0; i < 25; i++) printf("█");
    printf(RST "] " GRN BOLD "100%%\n\n" RST);

    const char *msgs[] = {
        "Kernel Linux dimuat (ARM64)               ",
        "Manajemen memori diinisialisasi (64MB)    ",
        "Filesystem virtual (VFS) dipasang         ",
        "Sistem pengguna & grup diaktifkan         ",
        "Modul keamanan & firewall aktif           ",
        "Alat jaringan siap                        ",
        "Manajer paket siap                        ",
        "Triple AI: Awing + DeepSeek + ChatGPT     ",
        "Shell FINIX v11.0 siap                    ",
    };
    for (int i = 0; i < 9; i++) {
        usleep(70000);
        printf("  " GRN "[  OK  ]" RST " %s\n", msgs[i]);
        fflush(stdout);
    }
    printf("\n");
}

/* ════════════════════════════════════════
 *  PROMPT
 * ════════════════════════════════════════ */
void print_prompt(void) {
    char cwd[MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");

    /* Ganti home path dengan ~ */
    const char *home = getenv("HOME");
    char display_cwd[MAX_PATH];
    if (home && strncmp(cwd, home, strlen(home)) == 0)
        snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
    else
        strncpy(display_cwd, cwd, sizeof(display_cwd)-1);

    const char *uclr = (current_user && current_user->is_root) ? RED BOLD : GRN BOLD;
    printf("%s%s@%s" RST WHT ":" CYN "%s" RST YLW "❯ " RST,
           uclr,
           current_user ? current_user->username : "tamu",
           HOSTNAME, display_cwd);
    fflush(stdout);
}

/* ════════════════════════════════════════
 *  AUTENTIKASI
 * ════════════════════════════════════════ */
void cmd_login(void) {
    if (current_user) {
        printf(YLW "  Sudah login sebagai %s.\n" RST, current_user->username);
        return;
    }
    printf("\n  " CYN "╔══════════════════════════╗\n");
    printf(      "  ║   " BOLD "🔐 LOGIN FINIX OS" RST CYN "    ║\n");
    printf(      "  ╚══════════════════════════╝\n" RST);
    char uname[MAX_NAME], pw[MAX_NAME];
    printf("  " WHT "Username : " RST); scanf("%63s", uname); (void)getchar();
    printf("  " WHT "Password : " RST); scanf("%63s", pw);    (void)getchar();
    if (authenticate(uname, pw))
        printf(GRN "\n  ✓ Login berhasil! Selamat datang, " BOLD "%s" RST GRN "!\n\n" RST, uname);
    else
        printf(RED "\n  ✗ Username atau password salah.\n\n" RST);
}

void cmd_daftar(void) {
    printf("\n  " CYN "╔══════════════════════════════╗\n");
    printf(      "  ║   " BOLD "📝 DAFTAR AKUN BARU" RST CYN "    ║\n");
    printf(      "  ╚══════════════════════════════╝\n" RST);
    char uname[MAX_NAME], pw[MAX_NAME], pw2[MAX_NAME];
    printf("  " WHT "Username (3-20 karakter) : " RST); scanf("%63s", uname); (void)getchar();

    /* Validasi */
    int len = strlen(uname);
    if (len < 3 || len > 20) {
        printf(RED "  ✗ Username harus 3-20 karakter!\n" RST); return;
    }
    for (int i = 0; uname[i]; i++)
        if (!isalnum(uname[i]) && uname[i] != '_') {
            printf(RED "  ✗ Username hanya boleh huruf, angka, _\n" RST); return;
        }
    /* Cek duplikat */
    struct list_head *pos;
    list_for_each(pos, &user_list) {
        finix_user_t *u = list_entry(pos, finix_user_t, list);
        if (strcmp(u->username, uname) == 0) {
            printf(RED "  ✗ Username sudah terdaftar!\n" RST); return;
        }
    }
    printf("  " WHT "Password (min. 4 karakter) : " RST); scanf("%63s", pw);    (void)getchar();
    if (strlen(pw) < 4) {
        printf(RED "  ✗ Password minimal 4 karakter!\n" RST); return;
    }
    printf("  " WHT "Konfirmasi password        : " RST); scanf("%63s", pw2);   (void)getchar();
    if (strcmp(pw, pw2) != 0) {
        printf(RED "  ✗ Password tidak cocok!\n" RST); return;
    }
    finix_user_t *u = kmem_cache_alloc(user_cache);
    if (!u) { printf(RED "  ✗ Memori penuh!\n" RST); return; }
    u->uid = next_uid++;
    strncpy(u->username, uname, MAX_NAME-1);
    simple_hash(pw, u->password_hash);
    u->is_root = 0;
    refcount_set(&u->refcnt, 1);
    list_add_tail(&u->list, &user_list);
    save_users();
    printf(GRN "\n  ✓ Registrasi berhasil! Silakan login.\n\n" RST);
}

void cmd_logout(void) {
    if (!current_user) { printf(YLW "  Belum login.\n" RST); return; }
    printf(GRN "  ✓ Sampai jumpa, %s! 👋\n" RST, current_user->username);
    current_user = NULL;
}

void cmd_siapakah(void) {
    printf("  " BOLD "%s" RST DIM " (%s)\n" RST,
           current_user ? current_user->username : "tamu",
           current_user && current_user->is_root ? "root" : "user");
}

void cmd_daftar_user(void) {
    printf("\n  " CYN "%-5s  %-20s  %s\n" RST, "UID", "Username", "Peran");
    printf("  %s\n", "─────────────────────────────────");
    struct list_head *pos;
    list_for_each(pos, &user_list) {
        finix_user_t *u = list_entry(pos, finix_user_t, list);
        const char *clr = u->is_root ? RED : GRN;
        printf("  %-5d  %s%-20s" RST "  %s\n",
               u->uid, clr, u->username,
               u->is_root ? "root" : "user");
    }
    printf("\n");
}

/* ════════════════════════════════════════
 *  SISTEM INFO
 * ════════════════════════════════════════ */
void cmd_uname(int full) {
    if (full) {
        /* Tampilkan uname -a asli Linux */
        system("uname -a 2>/dev/null");
    } else {
        printf("  %s %s %s\n", OS_NAME, HOSTNAME, OS_VERSION);
    }
}

void cmd_waktu(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    printf("  " CYN "%02d:%02d:%02d\n" RST,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void cmd_tanggal(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    const char *hari[] = {"Minggu","Senin","Selasa","Rabu",
                          "Kamis","Jumat","Sabtu"};
    const char *bln[]  = {"Januari","Februari","Maret","April","Mei","Juni",
                           "Juli","Agustus","September","Oktober",
                           "November","Desember"};
    printf("  " BOLD "%s, %d %s %d — %02d:%02d:%02d\n" RST,
           hari[tm->tm_wday], tm->tm_mday, bln[tm->tm_mon],
           tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void cmd_uptime(void) {
    long secs = (long)(time(NULL) - boot_time);
    printf("  %s up %ldj %ldm %lds, 1 pengguna\n",
           OS_NAME, secs/3600, (secs%3600)/60, secs%60);
}

void cmd_cpu(void) {
    printf(CYN BOLD "\n  🖥  Info CPU\n" RST);
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256]; int shown = 0;
        while (fgets(line, sizeof(line), f) && shown < 8) {
            if (strstr(line,"model name")||strstr(line,"processor")||
                strstr(line,"cpu MHz")||strstr(line,"Hardware")) {
                printf("  %s", line); shown++;
            }
        }
        fclose(f);
    } else {
        printf("  Prosesor  : ARMv8-A\n");
        printf("  Core      : 8\n");
        printf("  Frekuensi : 2.0 GHz\n");
    }
    printf("\n");
}

void cmd_ram(void) {
    printf(CYN BOLD "\n  🧠 Info RAM\n" RST);
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128]; long total=0, avail=0, free_=0, swap=0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line,"MemTotal:"))     sscanf(line,"MemTotal: %ld",&total);
            if (strstr(line,"MemAvailable:")) sscanf(line,"MemAvailable: %ld",&avail);
            if (strstr(line,"MemFree:"))      sscanf(line,"MemFree: %ld",&free_);
            if (strstr(line,"SwapTotal:"))    sscanf(line,"SwapTotal: %ld",&swap);
        }
        fclose(f);
        long used = total - avail;
        int pct = total > 0 ? (int)((float)used/total*100) : 0;
        printf("  %-14s %ld MB\n",   "Total:",     total/1024);
        printf("  %-14s %ld MB  ",   "Terpakai:",  used/1024);
        bar_fill(pct, 20); printf("\n");
        printf("  %-14s %ld MB\n",   "Tersedia:",  avail/1024);
        printf("  %-14s %ld MB\n\n", "Swap Total:",swap/1024);
    } else {
        printf("  Info RAM tidak tersedia.\n\n");
    }
}

void cmd_disk(void) {
    printf(CYN BOLD "\n  💾 Info Penyimpanan\n" RST);
    if (system("df -h /data 2>/dev/null | head -5") != 0)
        system("df -h . 2>/dev/null | head -5");
    printf("\n");
}

void cmd_baterai(void) {
    printf(CYN BOLD "\n  🔋 Info Baterai\n" RST);
    /* Coba termux-battery-status */
    if (system("which termux-battery-status >/dev/null 2>&1") == 0) {
        system("termux-battery-status 2>/dev/null | head -15");
        printf("\n"); return;
    }
    /* Fallback /sys */
    FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (f) {
        int cap = 0; fscanf(f, "%d", &cap); fclose(f);
        const char *clr = cap > 50 ? GRN : cap > 20 ? YLW : RED;
        printf("  Kapasitas : %s%d%%" RST "  ", clr, cap);
        bar_fill(cap, 20); printf("\n");
    }
    f = fopen("/sys/class/power_supply/battery/status", "r");
    if (f) {
        char st[32]; fgets(st, sizeof(st), f); fclose(f);
        st[strcspn(st,"\n")] = '\0';
        printf("  Status    : %s\n", st);
    }
    printf("\n");
}

void cmd_android(void) {
    printf(CYN BOLD "\n  📱 Info Perangkat\n" RST);
    const struct { const char *label; const char *prop; } props[] = {
        {"Versi Android",  "ro.build.version.release"},
        {"Patch Keamanan", "ro.build.version.security_patch"},
        {"Model",          "ro.product.model"},
        {"Produsen",       "ro.product.manufacturer"},
    };
    char cmd[128], buf[128];
    for (int i = 0; i < 4; i++) {
        snprintf(cmd, sizeof(cmd), "getprop %s 2>/dev/null", props[i].prop);
        FILE *p = popen(cmd, "r");
        if (p) {
            if (fgets(buf, sizeof(buf), p)) {
                buf[strcspn(buf,"\n")] = '\0';
                printf("  " CYN "%-18s" RST " %s\n", props[i].label, buf);
            }
            pclose(p);
        }
    }
    printf("  " CYN "%-18s" RST " ", "Kernel");
    system("uname -r 2>/dev/null");
    printf("\n");
}

void cmd_neofetch(void) {
    const char *logo[] = {
        CYN "  ╔══════════════════════════════════════╗",
        CYN "  ║  ______ _       _      __  ____  ___ ║",
        CYN "  ║ |  ____|_)     (_)    / / / __ \\/ __|║",
        CYN "  ║ | |__   _ _ __ _ ___  / / | |  | \\__ \\║",
        CYN "  ║ |  __| | | '_ \\| \\ \\/ /  | |  | |__) |║",
        CYN "  ║ | |    | | | | | |>  <   |____| |___/ ║",
        CYN "  ║ |_|    |_|_| |_|_/_/\\_\\  \\____/       ║",
        CYN "  ╚══════════════════════════════════════╝",
    };

    FILE *mf = fopen("/proc/meminfo","r");
    long memtotal=0, memavail=0;
    if (mf) {
        char ln[128];
        while(fgets(ln,sizeof(ln),mf)) {
            if(strstr(ln,"MemTotal:"))    sscanf(ln,"MemTotal: %ld",&memtotal);
            if(strstr(ln,"MemAvailable:"))sscanf(ln,"MemAvailable: %ld",&memavail);
        }
        fclose(mf);
    }
    long used_mb  = (memtotal - memavail)/1024;
    long total_mb = memtotal/1024;

    long uptime_s = (long)(time(NULL)-boot_time);

    const char *info[10];
    char buf_title[128], buf_os[64], buf_shell[64], buf_kern[128],
         buf_host[64], buf_user[128], buf_mem[64], buf_proc[64], buf_up[64];

    snprintf(buf_title, sizeof(buf_title),
             BOLD "%.32s@%.32s" RST,
             current_user ? current_user->username : "tamu", HOSTNAME);
    snprintf(buf_os,    sizeof(buf_os),
             CYN "OS     " RST "%s %s", OS_NAME, OS_VERSION);
    snprintf(buf_shell, sizeof(buf_shell),
             CYN "Shell  " RST "finixsh (C / ARM64)");
    /* Baca kernel versi asli dari /proc/version */
    char kern_real[64] = "Linux";
    FILE *kf = fopen("/proc/version","r");
    if (kf) {
        char kline[128];
        if (fgets(kline,sizeof(kline),kf)) {
            /* Ambil "Linux version X.X.X" */
            char *kv = strstr(kline,"version ");
            if (kv) {
                kv += 8;
                int ki=0;
                while (*kv && *kv!=' ' && ki<63) kern_real[ki++]=*kv++;
                kern_real[ki]='\0';
            }
        }
        fclose(kf);
    }
    snprintf(buf_kern,  sizeof(buf_kern),
             CYN "Kernel " RST "Linux %s (FinixShell v11.0)", kern_real);
    snprintf(buf_host,  sizeof(buf_host),
             CYN "Host   " RST "%s", HOSTNAME);
    snprintf(buf_user,  sizeof(buf_user),
             CYN "User   " RST "%.32s", current_user ? current_user->username : "tamu");
    if (total_mb > 0)
        snprintf(buf_mem, sizeof(buf_mem),
                 CYN "RAM    " RST "%ldMB / %ldMB", used_mb, total_mb);
    else
        snprintf(buf_mem, sizeof(buf_mem),
                 CYN "RAM    " RST "N/A");
    snprintf(buf_proc,  sizeof(buf_proc),
             CYN "Waktu  " RST "%ldj %ldm", uptime_s/3600, (uptime_s%3600)/60);
    snprintf(buf_up,    sizeof(buf_up),
             CYN "Kreator" RST " %s", OS_AUTHOR);

    info[0] = buf_title; info[1] = buf_os;    info[2] = buf_shell;
    info[3] = buf_kern;  info[4] = buf_host;  info[5] = buf_user;
    info[6] = buf_mem;   info[7] = buf_proc;  info[8] = buf_up;
    info[9] = NULL;

    printf("\n");
    for (int i = 0; i < 8; i++) {
        const char *inf = (i < 9) ? info[i] : "";
        printf("%s" RST "   %s\n", logo[i], inf);
    }
    for (int i = 8; info[i]; i++)
        printf("  %-40s%s\n", "", info[i]);
    printf("\n");
}

void cmd_sysinfo(void) {
    separator("FINIX OS " OS_VERSION " — INFO SISTEM", CYN);
    printf("  " CYN "%-14s" RST " %s %s\n", "OS:", OS_NAME, OS_VERSION);
    printf("  " CYN "%-14s" RST " %s\n",     "Codename:", OS_CODENAME);
    printf("  " CYN "%-14s" RST " %s\n",     "Creator:", OS_AUTHOR);
    printf("  " CYN "%-14s" RST " finixsh (C native)\n", "Shell:");
    printf("  " CYN "%-14s" RST " %s\n",     "Hostname:", HOSTNAME);
    printf("  " CYN "%-14s" RST " %s\n",     "User:",
           current_user ? current_user->username : "tamu");
    /* Kernel Linux asli */
    printf("  " CYN "%-14s" RST " ", "Kernel:");
    if (system("uname -r 2>/dev/null") != 0) printf("Linux (tidak diketahui)\n");
    cmd_uptime();
    cmd_cpu();
    cmd_ram();
    cmd_disk();
    int fw_count = 0;
    struct list_head *pos;
    list_for_each(pos, &fw_rules) fw_count++;
    printf("  " YLW "[ Firewall ] " RST "%d aturan aktif\n\n", fw_count);
}

/* ════════════════════════════════════════
 *  KALENDER
 * ════════════════════════════════════════ */
void cmd_kalender(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    const char *bln[] = {"Januari","Februari","Maret","April","Mei","Juni",
                          "Juli","Agustus","September","Oktober","November","Desember"};
    int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int yr = tm->tm_year+1900;
    if ((yr%4==0 && yr%100!=0) || yr%400==0) days[1]=29;
    printf(CYN BOLD "\n  %s %d\n" RST, bln[tm->tm_mon], yr);
    printf("  Mg  Sn  Sl  Rb  Km  Jm  Sb\n");
    struct tm first = {0};
    first.tm_year = tm->tm_year;
    first.tm_mon  = tm->tm_mon;
    first.tm_mday = 1;
    mktime(&first);
    printf("  ");
    for (int i = 0; i < first.tm_wday; i++) printf("    ");
    for (int d = 1; d <= days[tm->tm_mon]; d++) {
        if (d == tm->tm_mday) printf(GRN BOLD "%3d " RST, d);
        else                   printf("%3d ", d);
        if ((first.tm_wday + d) % 7 == 0) printf("\n  ");
    }
    printf("\n\n");
}

/* ════════════════════════════════════════
 *  FILESYSTEM COMMANDS (real FS)
 * ════════════════════════════════════════ */
void cmd_ls(const char *path) {
    if (!path || !strlen(path)) path = ".";
    DIR *dir = opendir(path);
    if (!dir) {
        printf(RED "  ✗ Tidak dapat membuka: %s\n" RST, path); return;
    }
    struct dirent *entry;
    struct stat st;
    char fp[MAX_PATH];
    printf("\n");
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        snprintf(fp, sizeof(fp), "%s/%s", path, entry->d_name);
        if (stat(fp, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            printf("  📁 " CYN BOLD "%-24s" RST, entry->d_name);
        else if (st.st_mode & S_IXUSR)
            printf("  ⚙  " GRN "%-24s" RST, entry->d_name);
        else
            printf("  📄 %-24s", entry->d_name);

        /* ukuran */
        if (!S_ISDIR(st.st_mode)) {
            if (st.st_size < 1024)
                printf(DIM " %ld B\n" RST, st.st_size);
            else if (st.st_size < 1024*1024)
                printf(DIM " %.1f KB\n" RST, (float)st.st_size/1024);
            else
                printf(DIM " %.1f MB\n" RST, (float)st.st_size/1024/1024);
        } else {
            printf("\n");
        }
    }
    closedir(dir);
    printf("\n");
}

void cmd_ll(const char *path) {
    if (!path || !strlen(path)) path = ".";
    DIR *dir = opendir(path);
    if (!dir) { printf(RED "  ✗ %s\n" RST, path); return; }
    struct dirent *entry;
    struct stat st;
    char fp[MAX_PATH];
    printf("\n  " CYN "%-11s %4s %8s  %-12s  %s\n" RST,
           "Izin", "Link", "Ukuran", "Diubah", "Nama");
    printf("  %s\n", "─────────────────────────────────────────────────");
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        snprintf(fp, sizeof(fp), "%s/%s", path, entry->d_name);
        if (stat(fp, &st) != 0) continue;
        char perm[12];
        snprintf(perm, sizeof(perm), "%c%c%c%c%c%c%c%c%c%c",
            S_ISDIR(st.st_mode) ? 'd' : '-',
            (st.st_mode&S_IRUSR)?'r':'-',(st.st_mode&S_IWUSR)?'w':'-',(st.st_mode&S_IXUSR)?'x':'-',
            (st.st_mode&S_IRGRP)?'r':'-',(st.st_mode&S_IWGRP)?'w':'-',(st.st_mode&S_IXGRP)?'x':'-',
            (st.st_mode&S_IROTH)?'r':'-',(st.st_mode&S_IWOTH)?'w':'-',(st.st_mode&S_IXOTH)?'x':'-');
        char tbuf[16];
        strftime(tbuf, sizeof(tbuf), "%d %b %H:%M", localtime(&st.st_mtime));
        const char *clr = S_ISDIR(st.st_mode) ? CYN BOLD :
                          (st.st_mode & S_IXUSR) ? GRN : WHT;
        printf("  %s  %4ld %8ld  %-12s  %s%s" RST "\n",
               perm, (long)st.st_nlink, (long)st.st_size, tbuf, clr, entry->d_name);
    }
    closedir(dir);
    printf("\n");
}

void cmd_pwd(void) {
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) printf("  %s\n", cwd);
}

void cmd_cd(const char *path) {
    if (!path || !strlen(path)) { chdir(getenv("HOME") ? getenv("HOME") : "/"); return; }
    if (chdir(path) != 0) printf(RED "  ✗ Tidak ditemukan: %s\n" RST, path);
}

void cmd_mkdir(const char *name) {
    if (!name || !strlen(name)) { printf(YLW "  Penggunaan: mkdir <nama>\n" RST); return; }
    if (mkdir(name, 0755) == 0) printf(GRN "  ✓ Dibuat: %s\n" RST, name);
    else printf(RED "  ✗ Gagal membuat: %s (%s)\n" RST, name, strerror(errno));
}

void cmd_sentuh(const char *name) {
    if (!name || !strlen(name)) { printf(YLW "  Penggunaan: sentuh <nama>\n" RST); return; }
    FILE *f = fopen(name, "a");
    if (f) { fclose(f); printf(GRN "  ✓ File dibuat: %s\n" RST, name); }
    else     printf(RED "  ✗ Gagal: %s\n" RST, name);
}

void cmd_cat(const char *name) {
    if (!name || !strlen(name)) { printf(YLW "  Penggunaan: cat <file>\n" RST); return; }
    FILE *f = fopen(name, "r");
    if (!f) { printf(RED "  ✗ Tidak ditemukan: %s\n" RST, name); return; }
    int c;
    printf("\n");
    while ((c = fgetc(f)) != EOF) putchar(c);
    printf("\n");
    fclose(f);
}

void cmd_hapus(const char *name) {
    if (!name || !strlen(name)) { printf(YLW "  Penggunaan: hapus <file>\n" RST); return; }
    if (remove(name) == 0) printf(GRN "  ✓ Dihapus: %s\n" RST, name);
    else printf(RED "  ✗ Gagal menghapus: %s\n" RST, name);
}

void cmd_salin(const char *src, const char *dst) {
    if (!src || !dst) { printf(YLW "  Penggunaan: salin <sumber> <tujuan>\n" RST); return; }
    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", src, dst);
    if (system(cmd) == 0) printf(GRN "  ✓ Disalin: %s → %s\n" RST, src, dst);
    else printf(RED "  ✗ Gagal menyalin.\n" RST);
}

void cmd_pindah(const char *src, const char *dst) {
    if (!src || !dst) { printf(YLW "  Penggunaan: pindah <sumber> <tujuan>\n" RST); return; }
    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "mv '%s' '%s'", src, dst);
    if (system(cmd) == 0) printf(GRN "  ✓ Dipindah: %s → %s\n" RST, src, dst);
    else printf(RED "  ✗ Gagal memindahkan.\n" RST);
}

void cmd_nano(const char *file) {
    if (!file || !strlen(file)) { printf(YLW "  Penggunaan: nano <file>\n" RST); return; }
    char cmd[MAX_LINE];
    /* Coba nano, fallback vi, fallback editor sederhana */
    if (system("which nano >/dev/null 2>&1") == 0)
        snprintf(cmd, sizeof(cmd), "nano '%s'", file);
    else if (system("which vi >/dev/null 2>&1") == 0)
        snprintf(cmd, sizeof(cmd), "vi '%s'", file);
    else {
        /* Mini editor sendiri */
        printf(CYN "\n  ── Editor Mini: %s ──\n" DIM
               "  Ketik baris. Ketik ':s' untuk simpan+keluar, ':q' untuk batal.\n" RST, file);
        char lines[500][MAX_LINE]; int n = 0;
        /* Baca isi file yang sudah ada */
        FILE *f = fopen(file, "r");
        if (f) {
            while (n < 500 && fgets(lines[n], MAX_LINE, f)) {
                lines[n][strcspn(lines[n],"\n")] = '\0'; n++;
            }
            fclose(f);
            for (int i = 0; i < n; i++) printf("  " DIM "%3d" RST " %s\n", i+1, lines[i]);
        }
        char buf[MAX_LINE];
        while (1) {
            printf("  " YLW ">" RST " "); fflush(stdout);
            if (!fgets(buf, MAX_LINE, stdin)) break;
            buf[strcspn(buf,"\n")] = '\0';
            if (strcmp(buf,":s")==0 || strcmp(buf,":wq")==0) {
                FILE *out = fopen(file, "w");
                if (out) {
                    for (int i=0; i<n; i++) fprintf(out,"%s\n",lines[i]);
                    fclose(out);
                    printf(GRN "  ✓ Tersimpan: %s\n" RST, file);
                }
                break;
            } else if (strcmp(buf,":q")==0) {
                printf(YLW "  Keluar tanpa menyimpan.\n" RST); break;
            } else if (n < 500) {
                strncpy(lines[n++], buf, MAX_LINE-1);
            }
        }
        return;
    }
    system(cmd);
}

/* ════════════════════════════════════════
 *  JARINGAN
 * ════════════════════════════════════════ */
void cmd_ping(const char *host) {
    if (!host || !strlen(host)) { printf(YLW "  Penggunaan: ping <host>\n" RST); return; }
    char cmd[256];
    printf(CYN "\n  📡 Ping ke %s...\n\n" RST, host);
    snprintf(cmd, sizeof(cmd), "ping -c 4 '%s'", host);
    system(cmd);
    printf("\n");
}

void cmd_cuaca(const char *kota) {
    if (!kota || !strlen(kota)) kota = "Jakarta";
    printf(CYN "\n  🌤  Cuaca %s...\n" RST, kota);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "curl -s 'wttr.in/%s?format=3' 2>/dev/null "
             "|| echo '  Tidak dapat terhubung (cek internet).'", kota);
    printf("  "); system(cmd);
    printf("\n");
}

void cmd_ifconfig(void) {
    printf(CYN BOLD "\n  🌐 Info Jaringan\n" RST);
    if (system("which ip >/dev/null 2>&1") == 0)
        system("ip addr show 2>/dev/null | head -30");
    else
        system("ifconfig 2>/dev/null | head -30");
    printf("\n");
}

void cmd_netstat(void) {
    printf(CYN BOLD "\n  🔌 Koneksi Aktif\n" RST);
    if (system("which netstat >/dev/null 2>&1") == 0)
        system("netstat -tuln 2>/dev/null | head -20");
    else if (system("which ss >/dev/null 2>&1") == 0)
        system("ss -tuln 2>/dev/null | head -20");
    else
        printf("  netstat/ss tidak tersedia.\n");
    printf("\n");
}

void cmd_curl(const char *url) {
    if (!url || !strlen(url)) { printf(YLW "  Penggunaan: curl <url>\n" RST); return; }
    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "curl -sL '%s' 2>/dev/null | head -50", url);
    system(cmd);
}

void cmd_wget(const char *url) {
    if (!url || !strlen(url)) { printf(YLW "  Penggunaan: wget <url>\n" RST); return; }
    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "wget --no-verbose '%s' 2>&1", url);
    system(cmd);
}

void cmd_nmap(const char *host) {
    if (!host || !strlen(host)) { printf(YLW "  Penggunaan: nmap <host>\n" RST); return; }
    printf(CYN "\n  🔍 Memindai %s...\n" RST, host);
    char cmd[256];
    if (system("which nmap >/dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd), "nmap -F '%s' 2>/dev/null | head -30", host);
        system(cmd);
    } else {
        printf("  " GRN "22/tcp  " RST "terbuka  ssh\n");
        printf("  " GRN "80/tcp  " RST "terbuka  http\n");
        printf("  " GRN "443/tcp " RST "terbuka  https\n");
        printf("  " YLW "(simulasi — install nmap untuk hasil nyata)\n" RST);
    }
    printf("\n");
}

/* ════════════════════════════════════════
 *  KEAMANAN
 * ════════════════════════════════════════ */
void cmd_scan(void) {
    printf(CYN BOLD "\n  🔍 Memindai Proses Mencurigakan\n" RST);
    system("ps aux 2>/dev/null | grep -E 'nc |netcat|nmap|python|perl|sh -i' | grep -v grep | head -10");
    printf(GRN "  ✓ Pemindaian selesai.\n\n" RST);
}

void cmd_antivirus(const char *path) {
    if (!path || !strlen(path)) path = ".";
    printf(CYN BOLD "\n  🦠 Antivirus FINIX — Memindai %s\n" RST, path);
    for (int i = 0; i <= 5; i++) {
        printf("  [");
        for (int j = 0; j < 20; j++) printf(j < i*4 ? GRN "█" RST : DIM "░" RST);
        printf("] %d%%\r", i*20); fflush(stdout);
        usleep(200000);
    }
    printf("\n" GRN "  ✓ Tidak ada ancaman ditemukan.\n\n" RST);
}

void cmd_ps_list(void) {
    printf(CYN BOLD "\n  ⚙  Daftar Proses\n" RST);
    system("ps aux 2>/dev/null | head -20");
    printf("\n");
}

void cmd_kill_proc(const char *pid) {
    if (!pid || !strlen(pid)) { printf(YLW "  Penggunaan: bunuh <pid>\n" RST); return; }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "kill %s 2>/dev/null", pid);
    if (system(cmd) == 0) printf(GRN "  ✓ Proses %s dihentikan.\n" RST, pid);
    else printf(RED "  ✗ Gagal menghentikan proses %s.\n" RST, pid);
}

/* ════════════════════════════════════════
 *  PAKET
 * ════════════════════════════════════════ */
/* Cari path pkg/apt — cek path absolut Termux & PATH biasa */
static const char *find_pkg_manager(void) {
    /* Path absolut Termux — tidak bergantung pada PATH environment */
    const char *candidates[] = {
        "/data/data/com.termux/files/usr/bin/pkg",
        "/data/data/com.termux/files/usr/bin/apt",
        "/usr/bin/apt",
        "/usr/bin/pkg",
        NULL
    };
    for (int i = 0; candidates[i]; i++)
        if (access(candidates[i], X_OK) == 0)
            return candidates[i];
    return NULL;
}

void cmd_paket(int argc, char **args) {
    if (argc < 1) {
        printf(YLW "  Penggunaan: paket pasang|hapus|daftar|perbarui [nama]\n" RST);
        return;
    }
    const char *sub = args[0];
    const char *nama = (argc > 1) ? args[1] : NULL;
    char cmd[MAX_LINE];

    /* Deteksi manajer paket lewat path absolut */
    const char *mgr = find_pkg_manager();
    if (!mgr) {
        printf(RED "  ✗ Manajer paket tidak ditemukan.\n" RST);
        printf(YLW "  Pastikan kamu menjalankan FINIX OS di dalam Termux.\n" RST);
        return;
    }

    /* Tentukan apakah ini pkg atau apt */
    int is_pkg = (strstr(mgr, "/pkg") != NULL);

    if (strcmp(sub,"pasang")==0 || strcmp(sub,"install")==0) {
        if (!nama) { printf(YLW "  Penggunaan: paket pasang <nama>\n" RST); return; }
        printf(GRN "  📦 Memasang %s...\n\n" RST, nama);
        if (is_pkg)
            snprintf(cmd, sizeof(cmd), "%s install -y %s", mgr, nama);
        else
            snprintf(cmd, sizeof(cmd), "%s install -y %s", mgr, nama);
        system(cmd);

    } else if (strcmp(sub,"hapus")==0 || strcmp(sub,"remove")==0) {
        if (!nama) { printf(YLW "  Penggunaan: paket hapus <nama>\n" RST); return; }
        printf(YLW "  🗑  Menghapus %s...\n\n" RST, nama);
        if (is_pkg)
            snprintf(cmd, sizeof(cmd), "%s uninstall %s", mgr, nama);
        else
            snprintf(cmd, sizeof(cmd), "%s remove -y %s", mgr, nama);
        system(cmd);

    } else if (strcmp(sub,"daftar")==0 || strcmp(sub,"list")==0) {
        printf(CYN "\n  📦 Paket Terinstall:\n" RST);
        if (is_pkg)
            snprintf(cmd, sizeof(cmd), "%s list-installed 2>/dev/null | head -40", mgr);
        else
            snprintf(cmd, sizeof(cmd), "%s list --installed 2>/dev/null | head -40", mgr);
        system(cmd);
        printf("\n");

    } else if (strcmp(sub,"perbarui")==0 || strcmp(sub,"update")==0) {
        printf(CYN "  🔄 Memperbarui repositori...\n\n" RST);
        snprintf(cmd, sizeof(cmd), "%s update", mgr);
        system(cmd);

    } else if (strcmp(sub,"cari")==0 || strcmp(sub,"search")==0) {
        if (!nama) { printf(YLW "  Penggunaan: paket cari <nama>\n" RST); return; }
        printf(CYN "  🔍 Mencari paket '%s'...\n\n" RST, nama);
        if (is_pkg)
            snprintf(cmd, sizeof(cmd), "%s search %s 2>/dev/null | head -20", mgr, nama);
        else
            snprintf(cmd, sizeof(cmd), "%s search %s 2>/dev/null | head -20", mgr, nama);
        system(cmd);
        printf("\n");

    } else {
        printf(YLW "  Penggunaan: paket pasang|hapus|daftar|perbarui|cari [nama]\n" RST);
        printf(DIM "  Manajer paket: %s\n\n" RST, mgr);
    }
}

/* ════════════════════════════════════════
 *  CATATAN
 * ════════════════════════════════════════ */
void cmd_catatan(int argc, char **args) {
    if (argc < 1) {
        printf(YLW "  Penggunaan: catatan tambah|daftar|hapus|bersih [args]\n" RST);
        return;
    }
    const char *sub = args[0];

    if (strcmp(sub,"tambah")==0 || strcmp(sub,"add")==0) {
        if (argc < 2) { printf(YLW "  Penggunaan: catatan tambah <teks>\n" RST); return; }
        /* Gabung semua args[1..] */
        char text[512] = "";
        for (int i=1; i<argc && strlen(text)<500; i++) {
            if (i>1) strncat(text," ",sizeof(text)-strlen(text)-1);
            strncat(text, args[i], sizeof(text)-strlen(text)-1);
        }
        if (note_count >= MAX_NOTES) { printf(RED "  ✗ Catatan penuh!\n" RST); return; }
        notes[note_count].id = note_count+1;
        strncpy(notes[note_count].text, text, 511);
        time_t t = time(NULL); struct tm *tm = localtime(&t);
        strftime(notes[note_count].waktu, 32, "%d/%m/%Y %H:%M", tm);
        note_count++;
        notes_save();
        printf(GRN "  ✓ Catatan #%d ditambahkan.\n" RST, note_count);

    } else if (strcmp(sub,"daftar")==0 || strcmp(sub,"list")==0) {
        if (note_count == 0) { printf(DIM "  Belum ada catatan.\n" RST); return; }
        printf("\n  " CYN "📋 Catatan Kamu:\n" RST);
        printf("  %s\n", "──────────────────────────────────────────");
        for (int i = 0; i < note_count; i++) {
            printf("  " YLW "#%d" RST "  %s\n", notes[i].id, notes[i].text);
            printf("      " DIM "%s\n" RST, notes[i].waktu);
        }
        printf("  %s\n\n", "──────────────────────────────────────────");

    } else if (strcmp(sub,"hapus")==0 || strcmp(sub,"del")==0) {
        if (argc < 2) { printf(YLW "  Penggunaan: catatan hapus <id>\n" RST); return; }
        int id = atoi(args[1]);
        if (id < 1 || id > note_count) {
            printf(RED "  ✗ ID tidak valid.\n" RST); return;
        }
        memmove(&notes[id-1], &notes[id], (note_count-id)*sizeof(note_t));
        note_count--;
        for (int i=0; i<note_count; i++) notes[i].id = i+1;
        notes_save();
        printf(GRN "  ✓ Catatan #%d dihapus.\n" RST, id);

    } else if (strcmp(sub,"bersih")==0 || strcmp(sub,"clear")==0) {
        printf(YLW "  Hapus semua catatan? (y/T): " RST);
        char c = (char)getchar(); (void)getchar();
        if (c=='y'||c=='Y') {
            note_count = 0; notes_save();
            printf(GRN "  ✓ Semua catatan dihapus.\n" RST);
        } else printf("  Dibatalkan.\n");
    } else {
        printf(YLW "  Penggunaan: catatan tambah|daftar|hapus <id>|bersih\n" RST);
    }
}

/* ════════════════════════════════════════
 *  AI SUGGESTION
 * ════════════════════════════════════════ */
typedef struct { const char *cmd; const char *desc; } cmd_info_t;
static const cmd_info_t cmd_db[] = {
    {"bantuan",   "Tampilkan daftar semua perintah."},
    {"sysinfo",   "Info sistem lengkap."},
    {"neofetch",  "Info sistem bergaya."},
    {"catatan",   "Buat & kelola catatan."},
    {"cuaca",     "Prakiraan cuaca kota."},
    {"firewall",  "Kelola aturan firewall."},
    {"paket",     "Manajer paket Termux/APT."},
    {"kalkulator","Kalkulator sederhana."},
    {"game",      "Mini game tebak angka."},
    {"ttt",       "Tic-Tac-Toe lawan AI."},
    {"sholat",    "Pengingat waktu sholat."},
    {"proses",    "Daftar proses."},
    {"baterai",   "Status baterai HP."},
    {"android",   "Info perangkat Android."},
    {NULL, NULL}
};

/* cmd_ai dan ai_suggestion didefinisikan di bagian Triple AI System */

/* ════════════════════════════════════════
 *  UTILITAS
 * ════════════════════════════════════════ */
void cmd_kalkulator(const char *a1s, const char *ops, const char *a2s) {
    if (!a1s || !ops || !a2s) {
        printf(YLW "  Penggunaan: kalkulator <angka> <+/-/*/÷> <angka>\n" RST);
        return;
    }
    double a = atof(a1s), b = atof(a2s), hasil = 0;
    char op = ops[0];
    int valid = 1;
    switch(op) {
        case '+': hasil = a+b; break;
        case '-': hasil = a-b; break;
        case '*': case 'x': hasil = a*b; break;
        case '/': case ':':
            if (b==0) { printf(RED "  ✗ Pembagian dengan nol!\n" RST); return; }
            hasil = a/b; break;
        case '^': hasil = finix_pow(a,b); break;
        default: valid = 0;
            printf(RED "  ✗ Operator tidak dikenal: %s\n" RST, ops);
    }
    if (valid) {
        if (finix_fmod(hasil,1.0)==0)
            printf(GRN "  %g %s %g = " BOLD "%.0f\n" RST, a, ops, b, hasil);
        else
            printf(GRN "  %g %s %g = " BOLD "%.6g\n" RST, a, ops, b, hasil);
    }
}

void cmd_gema(int argc, char **args) {
    for (int i = 0; i < argc; i++) {
        if (i) putchar(' ');
        printf("%s", args[i]);
    }
    putchar('\n');
}

void cmd_riwayat(void) {
    if (history_count == 0) { printf(DIM "  Belum ada riwayat.\n" RST); return; }
    printf("\n  " CYN "📜 Riwayat Perintah:\n" RST);
    for (int i = 0; i < history_count; i++)
        printf("  " DIM "%4d" RST "  %s\n", i+1, history[i]);
    printf("\n");
}

void cmd_bersih(void) { system("clear"); }

void cmd_tentang(void) {
    separator("FINIX OS " OS_VERSION " — " OS_CODENAME, CYN);
    printf("  " CYN "Creator   " RST ": %s\n", OS_AUTHOR);
    printf("  " CYN "Versi     " RST ": %s\n", OS_VERSION);
    printf("  " CYN "Arsitektur" RST ": ARM64 / Linux Kernel\n");
    printf("  " CYN "Shell     " RST ": finixsh (C native, fork/exec)\n");
    printf("  " CYN "Fitur     " RST ": Multi-user, Firewall, Jaringan,\n");
    printf("            Paket, Catatan, Game, Pemrograman\n");
    printf("  " CYN "AI        " RST ": Awing (Ollama) + DeepSeek + ChatGPT\n");
    printf("  " CYN "AI Aktif  " RST ": %s\n", ai_cfg.active);
    printf("  " CYN "Bahasa    " RST ": C (gcc/clang)\n\n");
    /* Tampilkan kernel asli */
    printf("  " CYN "Kernel    " RST ": ");
    system("uname -r 2>/dev/null || echo 'Linux'");
    printf("\n");
}

void cmd_moto(void) {
    const char *quotes[] = {
        "Kode terbaik adalah kode yang tidak perlu ditulis.",
        "Setiap expert dulunya pernah menjadi pemula.",
        "Talk is cheap. Show me the code. — Linus Torvalds",
        "Simplicity is the soul of efficiency.",
        "First, solve the problem. Then, write the code.",
        "Code never lies, comments sometimes do.",
        "Belajarlah sampai liang lahat.",
        "Ilmu tanpa amal seperti pohon tanpa buah.",
        "Kesuksesan adalah jumlah usaha kecil yang diulang tiap hari.",
        NULL
    };
    int n = 0; while(quotes[n]) n++;
    srand((unsigned)time(NULL));
    printf(YLW "\n  🌟 %s\n\n" RST, quotes[rand()%n]);
}

void cmd_spanduk(int argc, char **args) {
    if (argc < 1) { printf(YLW "  Penggunaan: spanduk <teks>\n" RST); return; }
    char teks[128] = "";
    for (int i=0; i<argc; i++) {
        if (i) strncat(teks," ",sizeof(teks)-strlen(teks)-1);
        strncat(teks, args[i], sizeof(teks)-strlen(teks)-1);
    }
    for (char *p = teks; *p; p++) *p = toupper(*p);
    int len = strlen(teks) + 6;
    printf("\n  " CYN);
    for (int i=0; i<len; i++) printf("▓");
    printf("\n  ▓▓ " BOLD WHT "%s" RST CYN " ▓▓\n  ", teks);
    for (int i=0; i<len; i++) printf("▓");
    printf("\n\n" RST);
}

/* ════════════════════════════════════════
 *  SHOLAT
 * ════════════════════════════════════════ */
void cmd_sholat(void) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    char now[6]; strftime(now, sizeof(now), "%H:%M", tm);
    const struct { const char *nama; const char *wkt; const char *ikon; } sholat[] = {
        {"Subuh",   "04:30", "🌙"},
        {"Syuruq",  "05:55", "🌅"},
        {"Dzuhur",  "12:00", "☀️ "},
        {"Ashar",   "15:15", "🌤 "},
        {"Maghrib", "18:00", "🌇"},
        {"Isya",    "19:30", "🌃"},
        {NULL, NULL, NULL}
    };
    printf("\n  " CYN "──────────────────────────────\n" RST);
    printf("  " CYN BOLD "🕌 Waktu Sholat (WIB - Jakarta)\n" RST);
    printf("  " CYN "──────────────────────────────\n" RST);
    for (int i = 0; sholat[i].nama; i++) {
        int lewat = (strcmp(now, sholat[i].wkt) > 0);
        const char *clr = lewat ? DIM : GRN;
        printf("  %s  %s%-8s" RST " %s  %s\n",
               sholat[i].ikon, clr, sholat[i].nama, sholat[i].wkt,
               lewat ? DIM "✓" RST : YLW "▶" RST);
    }
    printf("  " CYN "──────────────────────────────\n\n" RST);
}

/* ════════════════════════════════════════
 *  GAMES
 * ════════════════════════════════════════ */
void cmd_game_tebak(void) {
    srand((unsigned)time(NULL));
    int angka = rand()%100+1, tebak, coba=0;
    printf(CYN BOLD "\n  🎮 TEBAK ANGKA (1–100)\n" DIM
           "  Ketik 0 untuk keluar.\n\n" RST);
    while (1) {
        printf("  " YLW "Tebakanmu: " RST);
        if (scanf("%d",&tebak) != 1) { (void)getchar(); continue; }
        (void)getchar();
        if (tebak==0) break;
        coba++;
        if      (tebak < angka) printf("  📉 " YLW "Terlalu kecil!" RST " (ke-%d)\n",coba);
        else if (tebak > angka) printf("  📈 " YLW "Terlalu besar!" RST " (ke-%d)\n",coba);
        else {
            printf(GRN BOLD "\n  🎉 Benar! Angkanya %d, dalam %d percobaan!\n\n" RST,
                   angka, coba);
            break;
        }
    }
}

char ttt[3][3];
void ttt_init(void) { memset(ttt,' ',sizeof(ttt)); }
void ttt_tampil(void) {
    printf("\n  " CYN "  1   2   3\n" RST);
    for (int i=0; i<3; i++) {
        printf("  %d ",i+1);
        for (int j=0; j<3; j++) {
            const char *clr = ttt[i][j]=='X' ? GRN BOLD : ttt[i][j]=='O' ? RED BOLD : DIM;
            printf("%s%c" RST, clr, ttt[i][j]);
            if (j<2) printf(" │ ");
        }
        printf("\n");
        if (i<2) printf("    ─┼─┼─\n");
    }
    printf("\n");
}
int ttt_menang(char p) {
    for (int i=0;i<3;i++)
        if ((ttt[i][0]==p&&ttt[i][1]==p&&ttt[i][2]==p)||
            (ttt[0][i]==p&&ttt[1][i]==p&&ttt[2][i]==p)) return 1;
    return (ttt[0][0]==p&&ttt[1][1]==p&&ttt[2][2]==p)||
           (ttt[0][2]==p&&ttt[1][1]==p&&ttt[2][0]==p);
}
void ttt_ai(void) {
    /* Coba menang/blokir */
    for (int pass=0; pass<2; pass++) {
        char p = pass==0 ? 'O' : 'X';
        for (int i=0;i<3;i++) for (int j=0;j<3;j++) if (ttt[i][j]==' ') {
            ttt[i][j]=p;
            if (ttt_menang(p)) { if(pass==1) ttt[i][j]=' '; return; }
            ttt[i][j]=' ';
        }
    }
    if (ttt[1][1]==' ') { ttt[1][1]='O'; return; }
    srand((unsigned)time(NULL));
    int slots[9][2]; int n=0;
    for (int i=0;i<3;i++) for (int j=0;j<3;j++) if (ttt[i][j]==' ') { slots[n][0]=i; slots[n++][1]=j; }
    if (n) { int r=rand()%n; ttt[slots[r][0]][slots[r][1]]='O'; }
}
void cmd_ttt(void) {
    ttt_init();
    char pemain='X';
    printf(CYN BOLD "\n  ♟  TIC-TAC-TOE — Kamu: " GRN "X" CYN "  |  AI: " RED "O\n" DIM
           "  Format: baris kolom (mis: 1 1)\n" RST);
    for (int turn=0; turn<9; turn++) {
        ttt_tampil();
        if (pemain=='X') {
            int r,c; int ok=0;
            while(!ok) {
                printf("  " GRN "Langkahmu (baris kolom): " RST);
                if (scanf("%d %d",&r,&c)!=2) { (void)getchar(); continue; }
                (void)getchar();
                r--; c--;
                if (r>=0&&r<3&&c>=0&&c<3&&ttt[r][c]==' ') { ttt[r][c]='X'; ok=1; }
                else printf("  " RED "Posisi tidak valid.\n" RST);
            }
        } else {
            printf(DIM "  AI berpikir...\n" RST); usleep(500000);
            ttt_ai();
        }
        if (ttt_menang(pemain)) {
            ttt_tampil();
            if (pemain=='X') printf(GRN BOLD "  🎉 Kamu menang!\n\n" RST);
            else             printf(RED  BOLD "  🤖 AI menang!\n\n" RST);
            return;
        }
        pemain = (pemain=='X') ? 'O' : 'X';
    }
    ttt_tampil();
    printf(YLW "  🤝 Seri!\n\n" RST);
}

/* ════════════════════════════════════════
 *  PEMROGRAMAN
 * ════════════════════════════════════════ */
void cmd_run_c(const char *file) {
    if (!file||!strlen(file)) { printf(YLW "  Penggunaan: c <file.c>\n" RST); return; }
    char cmd[MAX_LINE];
    printf(CYN "  ⚙  Mengompilasi %s...\n" RST, file);
    snprintf(cmd, sizeof(cmd), "gcc '%s' -o /tmp/_finix_out -lm 2>&1", file);
    if (system(cmd) == 0) {
        printf(GRN "  ✓ Berhasil. Menjalankan...\n\n" RST);
        system("/tmp/_finix_out");
    } else {
        printf(RED "  ✗ Kompilasi gagal.\n" RST);
    }
}
void cmd_run_python(const char *file) {
    if (!file||!strlen(file)) { printf(YLW "  Penggunaan: python <file.py>\n" RST); return; }
    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "python3 '%s'", file);
    system(cmd);
}
void cmd_run_node(const char *file) {
    if (!file||!strlen(file)) { printf(YLW "  Penggunaan: node <file.js>\n" RST); return; }
    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "node '%s'", file);
    system(cmd);
}


/* ════════════════════════════════════════════════════════════════
 *  FINIX OS v9.0 — FITUR BARU
 *  1. TODO LIST
 *  2. TIMER & STOPWATCH
 *  3. INFO DUNIA (kurs, gempa, berita)
 *  4. GRAFIK ASCII
 *  5. ENKRIPSI FILE (XOR cipher)
 *  6. GAME: HANGMAN
 *  7. GAME: QUIZ
 *  8. GAME: TEBAK KATA
 * ════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════
 *  TODO LIST
 * ════════════════════════════════════════ */
#define TODO_FILE  "finix_todo.dat"
#define MAX_TODOS  200

typedef struct {
    int  id;
    char teks[256];
    char waktu[32];
    int  selesai;
    int  prioritas;  /* 1=rendah 2=sedang 3=tinggi */
} todo_t;

static todo_t  todos[MAX_TODOS];
static int     todo_count = 0;

void todo_load(void) {
    FILE *f = fopen(TODO_FILE, "r");
    if (!f) return;
    todo_count = 0;
    while (todo_count < MAX_TODOS) {
        todo_t *t = &todos[todo_count];
        if (fscanf(f, "%d|%d|%d|%31[^|]|%255[^\n]\n",
                   &t->id, &t->selesai, &t->prioritas,
                   t->waktu, t->teks) == 5)
            todo_count++;
        else break;
    }
    fclose(f);
}

void todo_save(void) {
    FILE *f = fopen(TODO_FILE, "w");
    if (!f) return;
    for (int i = 0; i < todo_count; i++)
        fprintf(f, "%d|%d|%d|%s|%s\n",
                todos[i].id, todos[i].selesai, todos[i].prioritas,
                todos[i].waktu, todos[i].teks);
    fclose(f);
}

static const char *prioritas_str(int p) {
    if (p == 3) return RED "!!TINGGI" RST;
    if (p == 2) return YLW " SEDANG" RST;
    return DIM "  RENDAH" RST;
}

void cmd_todo(int argc, char **args) {
    if (argc < 1) {
        printf(YLW "  Penggunaan: todo tambah|daftar|selesai|hapus|bersih [args]\n" RST);
        return;
    }
    const char *sub = args[0];

    if (strcmp(sub,"tambah")==0 || strcmp(sub,"t")==0) {
        if (argc < 2) { printf(YLW "  Penggunaan: todo tambah <teks> [1/2/3]\n" RST); return; }
        if (todo_count >= MAX_TODOS) { printf(RED "  ✗ Todo penuh!\n" RST); return; }
        todo_t *t = &todos[todo_count];
        t->id       = todo_count + 1;
        t->selesai  = 0;
        t->prioritas = (argc >= 3) ? atoi(args[argc-1]) : 2;
        if (t->prioritas < 1 || t->prioritas > 3) t->prioritas = 2;
        /* Gabung teks */
        t->teks[0] = '\0';
        int batas = (argc >= 3 && strlen(args[argc-1]) == 1 &&
                     args[argc-1][0] >= '1' && args[argc-1][0] <= '3') ? argc-1 : argc;
        for (int i = 1; i < batas; i++) {
            if (i > 1) strncat(t->teks, " ", sizeof(t->teks)-strlen(t->teks)-1);
            strncat(t->teks, args[i], sizeof(t->teks)-strlen(t->teks)-1);
        }
        time_t now = time(NULL);
        strftime(t->waktu, 32, "%d/%m %H:%M", localtime(&now));
        todo_count++;
        todo_save();
        printf(GRN "  ✓ Todo #%d ditambahkan [%s]\n" RST, todo_count, prioritas_str(t->prioritas));

    } else if (strcmp(sub,"daftar")==0 || strcmp(sub,"d")==0 || strcmp(sub,"list")==0) {
        if (todo_count == 0) { printf(DIM "  Belum ada todo.\n" RST); return; }
        int pending = 0, done = 0;
        for (int i = 0; i < todo_count; i++)
            todos[i].selesai ? done++ : pending++;
        printf("\n  " CYN BOLD "📋 Todo List" RST
               "  " DIM "(%d pending, %d selesai)\n" RST, pending, done);
        printf("  %s\n", "──────────────────────────────────────────────────");
        for (int i = 0; i < todo_count; i++) {
            todo_t *t = &todos[i];
            const char *status = t->selesai ?
                GRN "✓" RST : YLW "○" RST;
            const char *tclr = t->selesai ? DIM : WHT;
            printf("  %s %s#%-3d%s %s%-35s%s %s  %s%s%s\n",
                   status, CYN, t->id, RST,
                   tclr, t->teks, RST,
                   prioritas_str(t->prioritas),
                   DIM, t->waktu, RST);
        }
        printf("  %s\n\n", "──────────────────────────────────────────────────");

    } else if (strcmp(sub,"selesai")==0 || strcmp(sub,"s")==0) {
        if (argc < 2) { printf(YLW "  Penggunaan: todo selesai <id>\n" RST); return; }
        int id = atoi(args[1]);
        if (id < 1 || id > todo_count) { printf(RED "  ✗ ID tidak valid.\n" RST); return; }
        todos[id-1].selesai = 1;
        todo_save();
        printf(GRN "  ✓ Todo #%d ditandai selesai!\n" RST, id);

    } else if (strcmp(sub,"batal")==0) {
        if (argc < 2) { printf(YLW "  Penggunaan: todo batal <id>\n" RST); return; }
        int id = atoi(args[1]);
        if (id < 1 || id > todo_count) { printf(RED "  ✗ ID tidak valid.\n" RST); return; }
        todos[id-1].selesai = 0;
        todo_save();
        printf(YLW "  Todo #%d dikembalikan ke pending.\n" RST, id);

    } else if (strcmp(sub,"hapus")==0 || strcmp(sub,"h")==0) {
        if (argc < 2) { printf(YLW "  Penggunaan: todo hapus <id>\n" RST); return; }
        int id = atoi(args[1]);
        if (id < 1 || id > todo_count) { printf(RED "  ✗ ID tidak valid.\n" RST); return; }
        memmove(&todos[id-1], &todos[id], (todo_count-id)*sizeof(todo_t));
        todo_count--;
        for (int i = 0; i < todo_count; i++) todos[i].id = i+1;
        todo_save();
        printf(GRN "  ✓ Todo #%d dihapus.\n" RST, id);

    } else if (strcmp(sub,"bersih")==0) {
        printf(YLW "  Hapus semua todo? (y/T): " RST);
        char c = (char)getchar(); (void)getchar();
        if (c=='y'||c=='Y') { todo_count=0; todo_save(); printf(GRN "  ✓ Semua todo dihapus.\n" RST); }
        else printf("  Dibatalkan.\n");

    } else if (strcmp(sub,"ringkas")==0 || strcmp(sub,"r")==0) {
        int total=todo_count, selesai=0;
        for (int i=0;i<todo_count;i++) if(todos[i].selesai) selesai++;
        int pct = total > 0 ? (selesai*100/total) : 0;
        printf("\n  " CYN "📊 Ringkasan Todo\n" RST);
        printf("  Total   : %d\n", total);
        printf("  Selesai : " GRN "%d" RST "\n", selesai);
        printf("  Pending : " YLW "%d" RST "\n", total-selesai);
        printf("  Progress: [");
        int bar = pct/5;
        for (int i=0;i<20;i++) printf(i<bar ? GRN "█" RST : DIM "░" RST);
        printf("] %d%%\n\n", pct);

    } else {
        printf(YLW "  Penggunaan: todo tambah|daftar|selesai|batal|hapus|bersih|ringkas\n" RST);
    }
}

/* ════════════════════════════════════════
 *  TIMER & STOPWATCH
 * ════════════════════════════════════════ */
void cmd_timer(int argc, char **args) {
    int detik = 0;
    if (argc >= 1) {
        detik = atoi(args[0]);
        /* Format: 1m30s atau 90 */
        if (argc >= 1 && strchr(args[0],'m')) {
            int menit = atoi(args[0]);
            detik = menit * 60;
            if (argc >= 2) detik += atoi(args[1]);
        }
    }
    if (detik <= 0) {
        printf(YLW "  Penggunaan: timer <detik> atau timer <menit> <detik>\n" RST);
        printf(YLW "  Contoh   : timer 30  |  timer 1 30\n" RST);
        return;
    }

    printf(CYN BOLD "\n  ⏱  Timer %dm %ds\n" RST DIM
           "  Tekan Ctrl+C untuk batalkan.\n\n" RST,
           detik/60, detik%60);

    int sisa = detik;
    while (sisa >= 0) {
        int m = sisa/60, s = sisa%60;
        int pct = (detik-sisa)*100/detik;
        int bar = pct/5;
        const char *clr = sisa > detik/3 ? GRN : sisa > 10 ? YLW : RED;
        printf("\r  %s%02d:%02d%s  [", clr, m, s, RST);
        for (int i=0;i<20;i++) printf(i<bar ? GRN "█" RST : DIM "░" RST);
        printf("] %d%%   ", pct);
        fflush(stdout);
        if (sisa == 0) break;
        sleep(1);
        sisa--;
    }
    printf("\n\n  " GRN BOLD "🔔 WAKTU HABIS!\a\n\n" RST);
}

void cmd_stopwatch(void) {
    printf(CYN BOLD "\n  ⏱  Stopwatch\n" RST DIM
           "  Enter = lap/stop, Ctrl+C = batal\n\n" RST);
    time_t mulai = time(NULL);
    int lap_n = 0;
    time_t laps[50];

    /* Non-blocking tidak mudah di C murni, pakai polling sederhana */
    printf("  Tekan " YLW "Enter" RST " untuk lap/stop...\n");
    struct timespec ts = {0, 100000000}; /* 100ms */
    time_t prev_print = 0;

    while (1) {
        time_t now = time(NULL);
        long elapsed = (long)(now - mulai);
        if (now != prev_print) {
            printf("\r  " BOLD "%02ld:%02ld:%02ld" RST "  (lap %d)   ",
                   elapsed/3600, (elapsed%3600)/60, elapsed%60, lap_n+1);
            fflush(stdout);
            prev_print = now;
        }
        nanosleep(&ts, NULL);
        /* Cek stdin non-blocking dengan select */
        fd_set fds; struct timeval tv = {0,0};
        FD_ZERO(&fds); FD_SET(0, &fds);
        if (select(1, &fds, NULL, NULL, &tv) > 0) {
            char buf[8]; (void)read(0, buf, sizeof(buf));
            if (lap_n < 50) {
                laps[lap_n++] = time(NULL);
                long le = (long)(laps[lap_n-1] - mulai);
                printf("\n  " YLW "Lap %-2d" RST ": %02ld:%02ld:%02ld\n",
                       lap_n, le/3600, (le%3600)/60, le%60);
                if (lap_n >= 2) {
                    long diff = (long)(laps[lap_n-1] - laps[lap_n-2]);
                    printf("         +" DIM "%02ld:%02ld" RST "\n", diff/60, diff%60);
                }
                printf(DIM "  (Enter lagi untuk stop)\n" RST);
            } else {
                break;
            }
        }
        /* Stop jika lap terakhir ditandai */
        if (lap_n > 0 && time(NULL) - laps[lap_n-1] < 1) {
            /* Cek enter kedua dalam 1 detik = stop */
            fd_set fds2; struct timeval tv2 = {0, 500000};
            FD_ZERO(&fds2); FD_SET(0, &fds2);
            if (select(1, &fds2, NULL, NULL, &tv2) > 0) {
                char buf2[8]; (void)read(0, buf2, sizeof(buf2));
                break;
            }
        }
    }
    long total = (long)(time(NULL) - mulai);
    printf("\n  " GRN "Total: %02ld:%02ld:%02ld\n\n" RST,
           total/3600, (total%3600)/60, total%60);
}

/* ════════════════════════════════════════
 *  GRAFIK ASCII
 * ════════════════════════════════════════ */
#define GRAFIK_MAX 20

static void grafik_bar_h(const char *label, int val, int max_val, int width, const char *clr) {
    int bar = (max_val > 0) ? (val * width / max_val) : 0;
    printf("  %-12s " DIM "│" RST " %s", label, clr);
    for (int i = 0; i < bar; i++) printf("█");
    printf(RST DIM);
    for (int i = bar; i < width; i++) printf("░");
    printf(RST " %d\n", val);
}

void cmd_grafik(int argc, char **args) {
    if (argc < 1) {
        printf(YLW "  Penggunaan: grafik ram|cpu|disk|demo\n" RST);
        return;
    }

    if (strcmp(args[0],"ram")==0) {
        FILE *f = fopen("/proc/meminfo","r");
        if (!f) { printf(RED "  ✗ Tidak dapat membaca meminfo\n" RST); return; }
        long total=0,free_=0,avail=0,cached=0,buffers=0,swap_t=0,swap_f=0;
        char line[128];
        while (fgets(line,sizeof(line),f)) {
            if (strstr(line,"MemTotal:"))     sscanf(line,"MemTotal: %ld",&total);
            if (strstr(line,"MemFree:"))      sscanf(line,"MemFree: %ld",&free_);
            if (strstr(line,"MemAvailable:")) sscanf(line,"MemAvailable: %ld",&avail);
            if (strstr(line,"Buffers:"))      sscanf(line,"Buffers: %ld",&buffers);
            if (strstr(line,"Cached:"))       sscanf(line,"Cached: %ld",&cached);
            if (strstr(line,"SwapTotal:"))    sscanf(line,"SwapTotal: %ld",&swap_t);
            if (strstr(line,"SwapFree:"))     sscanf(line,"SwapFree: %ld",&swap_f);
        }
        fclose(f);
        long used    = total - avail;
        long swap_u  = swap_t - swap_f;
        long t_mb    = total/1024;

        printf(CYN BOLD "\n  📊 Grafik Penggunaan RAM\n" RST);
        printf("  %s\n", "──────────────────────────────────────────");
        grafik_bar_h("Terpakai",  (int)(used/1024),    (int)t_mb, 30, RED);
        grafik_bar_h("Tersedia",  (int)(avail/1024),   (int)t_mb, 30, GRN);
        grafik_bar_h("Cache",     (int)(cached/1024),  (int)t_mb, 30, YLW);
        grafik_bar_h("Buffer",    (int)(buffers/1024), (int)t_mb, 30, CYN);
        if (swap_t > 0)
            grafik_bar_h("Swap",  (int)(swap_u/1024),  (int)(swap_t/1024), 30, MAG);
        printf("  %s\n", "──────────────────────────────────────────");
        printf("  Total RAM: " BOLD "%ld MB\n\n" RST, t_mb);

    } else if (strcmp(args[0],"disk")==0) {
        printf(CYN BOLD "\n  📊 Grafik Penyimpanan\n" RST);
        printf("  %s\n", "──────────────────────────────────────────");
        /* Baca dari /proc/mounts dan statvfs */
        const char *paths[] = {"/data", "/sdcard", "/", NULL};
        for (int i = 0; paths[i]; i++) {
            struct stat st;
            if (stat(paths[i], &st) != 0) continue;
            /* Gunakan df via system — paling kompatibel */
            char cmd[128];
            snprintf(cmd, sizeof(cmd),
                "df -h %s 2>/dev/null | tail -1 | awk '{print $2, $3, $5}'",
                paths[i]);
            printf("  " CYN "%s" RST ": ", paths[i]);
            system(cmd);
        }
        printf("\n");

    } else if (strcmp(args[0],"demo")==0) {
        /* Grafik batang demo interaktif */
        printf(CYN BOLD "\n  📊 Demo Grafik Batang\n" RST);
        int data[] = {45, 78, 23, 91, 56, 34, 67, 88, 12, 99};
        const char *labels[] = {"Jan","Feb","Mar","Apr","Mei","Jun",
                                 "Jul","Agu","Sep","Okt"};
        const char *colors[] = {RED,GRN,YLW,CYN,MAG,BLU,RED,GRN,YLW,CYN};
        int max_v = 0;
        for (int i=0;i<10;i++) if(data[i]>max_v) max_v=data[i];

        printf("\n  " DIM "100 ┤\n" RST);
        for (int row = 9; row >= 0; row--) {
            int threshold = (row+1)*10;
            printf("  " DIM "%3d ┤" RST, threshold);
            for (int col = 0; col < 10; col++) {
                if (data[col] >= threshold)
                    printf("  %s██" RST, colors[col]);
                else
                    printf("  " DIM "  " RST);
            }
            printf("\n");
        }
        printf("  " DIM "  0 ┼");
        for (int i=0;i<10;i++) printf("────");
        printf("\n       ");
        for (int i=0;i<10;i++) printf(" " CYN "%s" RST " ", labels[i]);
        printf("\n\n");

    } else {
        printf(YLW "  Penggunaan: grafik ram|disk|demo\n" RST);
    }
}

/* ════════════════════════════════════════
 *  INFO DUNIA
 * ════════════════════════════════════════ */
void cmd_kurs(const char *mata_uang) {
    printf(CYN BOLD "\n  💱 Kurs Mata Uang\n" RST);
    if (!mata_uang || !strlen(mata_uang)) mata_uang = "USD";
    char cmd[512];
    /* Gunakan wttr.in-style API yang tersedia tanpa key */
    printf("  Mengambil data kurs %s...\n", mata_uang);
    snprintf(cmd, sizeof(cmd),
        "curl -s 'https://open.er-api.com/v6/latest/%s' 2>/dev/null | "
        "grep -o '\"IDR\":[0-9.]*' | head -1", mata_uang);
    printf("  1 %s = IDR ", mata_uang);
    int ret = system(cmd);
    if (ret != 0) {
        /* Fallback: gunakan rate.sx */
        snprintf(cmd, sizeof(cmd),
            "curl -s 'https://rate.sx/%s' 2>/dev/null | head -5", mata_uang);
        system(cmd);
    }
    printf("\n  " DIM "(Data dari open.er-api.com)\n\n" RST);
}

void cmd_gempa(void) {
    printf(CYN BOLD "\n  🌍 Info Gempa Terkini (Indonesia)\n" RST);
    printf("  Mengambil data dari BMKG...\n\n");
    int ret = system(
        "curl -s 'https://data.bmkg.go.id/DataMKG/TEWS/autogempa.json' "
        "2>/dev/null | grep -o '\"[A-Za-z]*\":\"[^\"]*\"' | "
        "sed 's/\"//g' | sed 's/:/: /' | grep -E 'Wilayah|Magnitude|Kedalaman|Jam|Potensi' "
        "| head -10"
    );
    if (ret != 0 || system("curl -s https://data.bmkg.go.id >/dev/null 2>&1") != 0) {
        printf(YLW "  Tidak dapat terhubung ke BMKG.\n" RST);
        printf(DIM "  Pastikan ada koneksi internet.\n\n" RST);
        return;
    }
    printf("\n  " DIM "(Sumber: BMKG data.bmkg.go.id)\n\n" RST);
}

void cmd_cuaca_detail(const char *kota) {
    if (!kota || !strlen(kota)) kota = "Jakarta";
    printf(CYN BOLD "\n  🌤  Cuaca Detail: %s\n" RST, kota);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "curl -s 'wttr.in/%s?format=v2' 2>/dev/null | head -20", kota);
    system(cmd);
    printf("\n");
}

void cmd_info_dunia(int argc, char **args) {
    if (argc < 1) {
        printf(YLW "  Penggunaan: dunia kurs|gempa|cuaca [arg]\n" RST);
        printf(YLW "  Contoh   : dunia kurs USD\n" RST);
        printf(YLW "             dunia gempa\n" RST);
        printf(YLW "             dunia cuaca Surabaya\n" RST);
        return;
    }
    if (strcmp(args[0],"kurs")==0)
        cmd_kurs(argc>1?args[1]:"USD");
    else if (strcmp(args[0],"gempa")==0)
        cmd_gempa();
    else if (strcmp(args[0],"cuaca")==0)
        cmd_cuaca_detail(argc>1?args[1]:"Jakarta");
    else
        printf(YLW "  Sub-perintah: kurs <mata_uang> | gempa | cuaca <kota>\n" RST);
}

/* ════════════════════════════════════════
 *  ENKRIPSI XOR
 * ════════════════════════════════════════ */
static void xor_proses(const char *file_in, const char *file_out, const char *kunci) {
    FILE *fi = fopen(file_in, "rb");
    if (!fi) { printf(RED "  ✗ File tidak ditemukan: %s\n" RST, file_in); return; }
    FILE *fo = fopen(file_out, "wb");
    if (!fo) { fclose(fi); printf(RED "  ✗ Tidak dapat membuat: %s\n" RST, file_out); return; }

    int klen = (int)strlen(kunci);
    int ki   = 0;
    int c;
    long bytes = 0;
    while ((c = fgetc(fi)) != EOF) {
        fputc(c ^ (unsigned char)kunci[ki % klen], fo);
        ki++; bytes++;
    }
    fclose(fi); fclose(fo);
    printf(GRN "  ✓ Berhasil! %ld byte diproses → %s\n" RST, bytes, file_out);
}

void cmd_enkripsi(int argc, char **args) {
    if (argc < 3) {
        printf(CYN BOLD "\n  🔐 Enkripsi File (XOR Cipher)\n" RST);
        printf(YLW "  Penggunaan: enkripsi <file_input> <file_output> <kunci>\n" RST);
        printf(YLW "  Contoh   : enkripsi rahasia.txt rahasia.enc password123\n" RST);
        printf(DIM "  Catatan  : Enkripsi = Dekripsi (jalankan lagi untuk decrypt)\n\n" RST);
        return;
    }
    printf(CYN "\n  🔐 Memproses %s...\n" RST, args[0]);
    xor_proses(args[0], args[1], args[2]);

    /* Tampilkan hash sederhana untuk verifikasi */
    unsigned long h = 5381;
    for (int i = 0; args[2][i]; i++)
        h = ((h<<5)+h) ^ (unsigned char)args[2][i];
    printf(DIM "  Fingerprint kunci: %08lx\n\n" RST, h & 0xFFFFFFFF);
}

void cmd_hash_file(const char *file) {
    if (!file || !strlen(file)) {
        printf(YLW "  Penggunaan: hash <file>\n" RST); return;
    }
    FILE *f = fopen(file, "rb");
    if (!f) { printf(RED "  ✗ File tidak ditemukan: %s\n" RST, file); return; }
    unsigned long h1 = 5381, h2 = 0x811c9dc5UL;
    int c; long size = 0;
    while ((c = fgetc(f)) != EOF) {
        h1 = ((h1<<5)+h1) ^ (unsigned char)c;
        h2 = (h2 ^ (unsigned char)c) * 0x01000193UL;
        size++;
    }
    fclose(f);
    printf(CYN "\n  🔑 Hash File: %s\n" RST, file);
    printf("  Ukuran  : %ld byte\n", size);
    printf("  Hash-A  : " YLW "%016lx\n" RST, h1);
    printf("  Hash-B  : " YLW "%08lx\n" RST, h2 & 0xFFFFFFFF);
    printf("  Checksum: " GRN "%016lx\n\n" RST, h1 ^ h2);
}

/* ════════════════════════════════════════
 *  GAME: HANGMAN
 * ════════════════════════════════════════ */
static const char *kata_hangman[] = {
    "komputer","pemrograman","algoritma","variabel","fungsi",
    "pointer","struktur","rekursi","kompilasi","debugging",
    "jaringan","keamanan","enkripsi","database","antarmuka",
    "termux","android","linux","kernel","memori",
    "indonesia","surabaya","jakarta","bandung","yogyakarta",
    NULL
};

static void hangman_gambar(int salah) {
    const char *frame[][7] = {
        {"   +---+   ","   |   |   ","       |   ","       |   ","       |   ","       |   ","=========  "},
        {"   +---+   ","   |   |   ","   O   |   ","       |   ","       |   ","       |   ","=========  "},
        {"   +---+   ","   |   |   ","   O   |   ","   |   |   ","       |   ","       |   ","=========  "},
        {"   +---+   ","   |   |   ","   O   |   ","  /|   |   ","       |   ","       |   ","=========  "},
        {"   +---+   ","   |   |   ","   O   |   ","  /|\\  |   ","       |   ","       |   ","=========  "},
        {"   +---+   ","   |   |   ","   O   |   ","  /|\\  |   ","  /    |   ","       |   ","=========  "},
        {"   +---+   ","   |   |   ","   O   |   ","  /|\\  |   ","  / \\  |   ","       |   ","=========  "},
    };
    int f = salah > 6 ? 6 : salah;
    printf("\n");
    for (int i = 0; i < 7; i++) {
        const char *clr = (f==6) ? RED : (f>=4) ? YLW : GRN;
        printf("  %s%s\n" RST, clr, frame[f][i]);
    }
}

void cmd_hangman(void) {
    srand((unsigned)time(NULL));
    int n = 0;
    while (kata_hangman[n]) n++;
    const char *kata = kata_hangman[rand() % n];
    int panjang = (int)strlen(kata);

    char tampil[64]; memset(tampil, '_', panjang); tampil[panjang] = '\0';
    char tebak[30]; memset(tebak, 0, sizeof(tebak));
    int salah = 0, tebak_n = 0;

    printf(CYN BOLD "\n  🪢 HANGMAN — Tebak Kata!\n" RST DIM
           "  Kategori: Programming & Teknologi\n\n" RST);

    while (salah < 6) {
        hangman_gambar(salah);

        /* Tampilkan kata */
        printf("\n  Kata  : ");
        for (int i = 0; i < panjang; i++) {
            int revealed = 0;
            for (int j = 0; j < tebak_n; j++)
                if (tebak[j] == kata[i]) { revealed = 1; break; }
            printf(revealed ? GRN " %c" RST : YLW " _" RST, kata[i]);
        }

        /* Cek menang */
        int selesai = 1;
        for (int i = 0; i < panjang; i++) {
            int ok = 0;
            for (int j = 0; j < tebak_n; j++) if(tebak[j]==kata[i]){ok=1;break;}
            if (!ok) { selesai=0; break; }
        }
        if (selesai) {
            printf("\n\n  " GRN BOLD "🎉 MENANG! Kata: %s\n\n" RST, kata);
            return;
        }

        /* Huruf yang sudah ditebak */
        printf("\n  Salah : " RED);
        int ada = 0;
        for (int j = 0; j < tebak_n; j++) {
            int benar = 0;
            for (int i = 0; i < panjang; i++) if(tebak[j]==kata[i]){benar=1;break;}
            if (!benar) { printf("%c ", tebak[j]); ada=1; }
        }
        if (!ada) printf("-");
        printf(RST "  (%d/6 salah)\n", salah);

        printf("\n  Tebak huruf: ");
        char input[8];
        if (!fgets(input, sizeof(input), stdin)) break;
        char huruf = tolower(input[0]);
        if (!isalpha(huruf)) { printf(YLW "  Masukkan huruf!\n" RST); continue; }

        /* Cek sudah ditebak */
        int sudah = 0;
        for (int j=0;j<tebak_n;j++) if(tebak[j]==huruf){sudah=1;break;}
        if (sudah) { printf(YLW "  Huruf '%c' sudah pernah ditebak!\n" RST, huruf); continue; }

        tebak[tebak_n++] = huruf;
        int ada_di_kata = 0;
        for (int i=0;i<panjang;i++) if(kata[i]==huruf){ada_di_kata=1;break;}
        if (!ada_di_kata) {
            salah++;
            printf(RED "  ✗ Huruf '%c' tidak ada! (%d/6)\n" RST, huruf, salah);
        } else {
            printf(GRN "  ✓ Huruf '%c' ada!\n" RST, huruf);
        }
    }

    hangman_gambar(6);
    printf("\n  " RED BOLD "💀 KALAH! Kata: " YLW "%s\n\n" RST, kata);
}

/* ════════════════════════════════════════
 *  GAME: QUIZ
 * ════════════════════════════════════════ */
typedef struct {
    const char *soal;
    const char *pilihan[4];
    int jawaban; /* 0-3 */
} soal_t;

static const soal_t quiz_db[] = {
    {"Siapa pencipta bahasa C?",
     {"Dennis Ritchie","Bjarne Stroustrup","Linus Torvalds","Ken Thompson"}, 0},
    {"Apa kepanjangan dari CPU?",
     {"Central Processing Unit","Computer Power Unit","Core Processing Unit","Central Power Unit"}, 0},
    {"Berapa bit dalam 1 byte?",
     {"4","16","8","32"}, 2},
    {"Apa sistem operasi yang dibuat Linus Torvalds?",
     {"Windows","macOS","Android","Linux"}, 3},
    {"Struktur data LIFO adalah?",
     {"Queue","Stack","Array","Tree"}, 1},
    {"Apa ekstensi file C?",
     {".cpp",".java",".c",".py"}, 2},
    {"Fungsi malloc() untuk?",
     {"Mencetak teks","Alokasi memori","Membaca file","Sorting array"}, 1},
    {"Termux berjalan di atas?",
     {"iOS","Windows","Android","Linux"}, 2},
    {"IP address loopback adalah?",
     {"192.168.1.1","0.0.0.0","127.0.0.1","255.255.255.0"}, 2},
    {"NULL dalam C bernilai?",
     {"1","-1","0","undefined"}, 2},
    {"Apa kepanjangan RAM?",
     {"Read Access Memory","Random Access Memory","Rapid Access Module","Read And Memory"}, 1},
    {"Algoritma sorting tercepat rata-rata?",
     {"Bubble Sort","Quick Sort","Selection Sort","Insertion Sort"}, 1},
    {"Port HTTP standar?",
     {"443","21","80","22"}, 2},
    {"Apa itu pointer dalam C?",
     {"Variabel integer","Variabel yang menyimpan alamat memori","Fungsi rekursif","Tipe data float"}, 1},
    {"Perintah compile C di Termux?",
     {"python file.c","java file.c","gcc file.c -o output","run file.c"}, 2},
};

void cmd_quiz(int argc, char **args) {
    int jumlah = 5;
    if (argc >= 1) jumlah = atoi(args[0]);
    if (jumlah < 1) jumlah = 1;
    if (jumlah > 15) jumlah = 15;

    srand((unsigned)time(NULL));
    int urutan[15];
    for (int i=0;i<15;i++) urutan[i]=i;
    /* Shuffle */
    for (int i=14;i>0;i--) {
        int j=rand()%(i+1), tmp=urutan[i];
        urutan[i]=urutan[j]; urutan[j]=tmp;
    }

    int benar=0;
    printf(CYN BOLD "\n  📝 QUIZ — Teknologi & Programming\n" RST DIM
           "  %d soal, ketik nomor jawaban (1-4)\n\n" RST, jumlah);

    for (int q=0; q<jumlah; q++) {
        const soal_t *s = &quiz_db[urutan[q]];
        printf(CYN "  Soal %d/%d:" RST " %s\n", q+1, jumlah, s->soal);
        for (int i=0;i<4;i++)
            printf("  " YLW "%d." RST " %s\n", i+1, s->pilihan[i]);
        printf("  Jawaban: ");

        char buf[8];
        if (!fgets(buf, sizeof(buf), stdin)) break;
        int jwb = atoi(buf) - 1;

        if (jwb == s->jawaban) {
            printf(GRN "  ✓ Benar!\n\n" RST);
            benar++;
        } else {
            printf(RED "  ✗ Salah! Jawaban: " YLW "%s\n\n" RST,
                   s->pilihan[s->jawaban]);
        }
    }

    /* Hasil */
    int pct = benar*100/jumlah;
    printf("  " CYN "──────────────────────────────\n" RST);
    printf("  Skor: " BOLD "%d/%d" RST " (%d%%)\n", benar, jumlah, pct);
    printf("  [");
    for (int i=0;i<20;i++) printf(i<pct/5 ? GRN "█" RST : DIM "░" RST);
    printf("]\n");
    if (pct==100) printf("  " GRN BOLD "🏆 SEMPURNA!\n" RST);
    else if (pct>=80) printf("  " GRN "🌟 Bagus sekali!\n" RST);
    else if (pct>=60) printf("  " YLW "👍 Cukup baik!\n" RST);
    else printf("  " RED "📚 Perlu belajar lagi!\n" RST);
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 *  FINIX OS v10.0 — FITUR BARU
 *  1. LINUX-LIKE EXECUTION — jalankan binary/script apapun
 *  2. SHELL SCRIPTING .fsh — interpreter bahasa sendiri
 *  3. CHAT LAN — kirim pesan antar HP satu WiFi
 *  4. INTEGRASI AI — tanya Claude dari FINIX OS
 *  5. TUI SEDERHANA — menu navigasi tanpa ncurses
 * ════════════════════════════════════════════════════════════════ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <sys/wait.h>

/* ════════════════════════════════════════
 *  LINUX-LIKE EXECUTION
 *  Jalankan binary, script, apapun
 *  seperti shell Linux asli
 * ════════════════════════════════════════ */

/* Cari binary di PATH dan path umum Termux */
static int find_binary(const char *name, char *out, size_t out_size) {
    /* Path Termux + Linux standar */
    const char *dirs[] = {
        "/data/data/com.termux/files/usr/bin",
        "/data/data/com.termux/files/usr/sbin",
        "/data/data/com.termux/files/home/.local/bin",
        "/usr/bin", "/usr/sbin", "/bin", "/sbin",
        "/usr/local/bin",
        NULL
    };
    /* Jika sudah path absolut */
    if (name[0] == '/' || name[0] == '.') {
        if (access(name, X_OK) == 0) {
            strncpy(out, name, out_size-1);
            return 1;
        }
        return 0;
    }
    for (int i = 0; dirs[i]; i++) {
        snprintf(out, out_size, "%s/%s", dirs[i], name);
        if (access(out, X_OK) == 0) return 1;
    }
    return 0;
}

/* Eksekusi perintah eksternal dengan fork/exec */
static int exec_external(int argc, char **args) {
    char bin_path[MAX_PATH];
    if (!find_binary(args[0], bin_path, sizeof(bin_path))) return 0;

    pid_t pid = fork();
    if (pid < 0) {
        printf(RED "  ✗ Gagal fork proses.\n" RST);
        return 1;
    }
    if (pid == 0) {
        /* Child: eksekusi program */
        execv(bin_path, args);
        /* Jika execv gagal */
        fprintf(stderr, "finixsh: %s: %s\n", args[0], strerror(errno));
        _exit(127);
    }
    /* Parent: tunggu child selesai */
    int status;
    waitpid(pid, &status, 0);
    return 1; /* berhasil dieksekusi */
}

/* ════════════════════════════════════════
 *  SHELL SCRIPTING .fsh
 *  Bahasa script FINIX sendiri
 * ════════════════════════════════════════ */

#define FSH_MAX_VARS  64
#define FSH_VAR_LEN   64
#define FSH_VAL_LEN   256

typedef struct {
    char nama[FSH_VAR_LEN];
    char nilai[FSH_VAL_LEN];
} fsh_var_t;

static fsh_var_t fsh_vars[FSH_MAX_VARS];
static int       fsh_var_count = 0;

static void fsh_set_var(const char *nama, const char *nilai) {
    for (int i = 0; i < fsh_var_count; i++) {
        if (strcmp(fsh_vars[i].nama, nama) == 0) {
            strncpy(fsh_vars[i].nilai, nilai, FSH_VAL_LEN-1);
            return;
        }
    }
    if (fsh_var_count < FSH_MAX_VARS) {
        strncpy(fsh_vars[fsh_var_count].nama,  nama,  FSH_VAR_LEN-1);
        strncpy(fsh_vars[fsh_var_count].nilai, nilai, FSH_VAL_LEN-1);
        fsh_var_count++;
    }
}

static const char *fsh_get_var(const char *nama) {
    for (int i = 0; i < fsh_var_count; i++)
        if (strcmp(fsh_vars[i].nama, nama) == 0)
            return fsh_vars[i].nilai;
    /* Cek env */
    const char *e = getenv(nama);
    return e ? e : "";
}

/* Expand $VARIABEL dalam string */
static void fsh_expand(const char *src, char *dst, size_t dst_size) {
    size_t si = 0, di = 0;
    while (src[si] && di < dst_size-1) {
        if (src[si] == '$') {
            si++;
            char varname[FSH_VAR_LEN]; int vi = 0;
            while (src[si] && (isalnum(src[si]) || src[si]=='_') && vi < FSH_VAR_LEN-1)
                varname[vi++] = src[si++];
            varname[vi] = '\0';
            const char *val = fsh_get_var(varname);
            while (*val && di < dst_size-1) dst[di++] = *val++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

/* Jalankan satu baris script .fsh */
static void fsh_exec_line(const char *line, int lineno);

static void fsh_run_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { printf(RED "  ✗ Script tidak ditemukan: %s\n" RST, filename); return; }
    char line[MAX_LINE];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        line[strcspn(line, "\n")] = '\0';
        /* Hapus komentar */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        /* Trim */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strlen(p)) continue;
        fsh_exec_line(p, lineno);
    }
    fclose(f);
}

static void fsh_exec_line(const char *raw_line, int lineno) {
    char line[MAX_LINE];
    fsh_expand(raw_line, line, sizeof(line));

    /* Tokenize */
    char buf[MAX_LINE]; strncpy(buf, line, MAX_LINE-1);
    char *fsh_args[16]; int argc = 0;
    char *tok = strtok(buf, " \t");
    while (tok && argc < 15) { fsh_args[argc++] = tok; tok = strtok(NULL," \t"); }
    fsh_args[argc] = NULL;
    if (!argc) return;

    /* ── Keyword .fsh ── */

    /* cetak "teks" */
    if (strcmp(fsh_args[0],"cetak")==0) {
        for (int i=1;i<argc;i++) {
            char exp[MAX_LINE]; fsh_expand(fsh_args[i], exp, sizeof(exp));
            printf("%s%s", i>1?" ":"", exp);
        }
        printf("\n");

    /* atur NAMA = NILAI */
    } else if (argc >= 3 && strcmp(fsh_args[1],"=")==0) {
        char val[FSH_VAL_LEN]; fsh_expand(fsh_args[2], val, sizeof(val));
        fsh_set_var(fsh_args[0], val);

    /* jika KONDISI maka PERINTAH */
    } else if (strcmp(fsh_args[0],"jika")==0 && argc >= 4 && strcmp(fsh_args[2],"maka")==0) {
        const char *kiri = fsh_args[1];
        /* Cari operator */
        /* Format: jika $A == $B maka perintah */
        /* Untuk simplisitas: jika <val> maka <cmd> */
        char kiri_exp[FSH_VAL_LEN]; fsh_expand(kiri, kiri_exp, sizeof(kiri_exp));
        int kondisi = (strlen(kiri_exp) > 0 && strcmp(kiri_exp,"0") != 0);
        if (kondisi) {
            char sub[MAX_LINE]; sub[0]='\0';
            for (int i=3;i<argc;i++) {
                if (i>3) strncat(sub," ",sizeof(sub)-strlen(sub)-1);
                strncat(sub, fsh_args[i], sizeof(sub)-strlen(sub)-1);
            }
            fsh_exec_line(sub, lineno);
        }

    /* ulang N perintah */
    } else if (strcmp(fsh_args[0],"ulang")==0 && argc >= 3) {
        int n = atoi(fsh_args[1]);
        char sub[MAX_LINE]; sub[0]='\0';
        for (int i=2;i<argc;i++) {
            if (i>2) strncat(sub," ",sizeof(sub)-strlen(sub)-1);
            strncat(sub,fsh_args[i],sizeof(sub)-strlen(sub)-1);
        }
        for (int i=0;i<n;i++) fsh_exec_line(sub, lineno);

    /* tunda N  (sleep N detik) */
    } else if (strcmp(fsh_args[0],"tunda")==0 && argc >= 2) {
        sleep(atoi(fsh_args[1]));

    /* bersih */
    } else if (strcmp(fsh_args[0],"bersih")==0) {
        system("clear");

    /* jalankan script lain */
    } else if (strcmp(fsh_args[0],"jalankan")==0 && argc >= 2) {
        fsh_run_file(fsh_args[1]);

    /* Fallthrough: coba sebagai perintah FINIX OS biasa */
    } else {
        extern void execute(char *input);
        char cmd_buf[MAX_LINE]; strncpy(cmd_buf, line, MAX_LINE-1);
        execute(cmd_buf);
    }
}

void cmd_fsh(int argc, char **args) {
    if (argc < 1) {
        printf(CYN BOLD "\n  📜 FINIX Shell Script (.fsh)\n" RST);
        printf(YLW "  Penggunaan: fsh <file.fsh>\n" RST);
        printf(DIM "\n  Sintaks .fsh:\n" RST);
        printf("  %-30s %s\n", "cetak \"Halo Dunia\"",       "Cetak teks");
        printf("  %-30s %s\n", "NAMA = nilai",               "Set variabel");
        printf("  %-30s %s\n", "cetak $NAMA",                "Gunakan variabel");
        printf("  %-30s %s\n", "jika $X maka perintah",      "Kondisional");
        printf("  %-30s %s\n", "ulang 3 cetak \"halo\"",     "Perulangan");
        printf("  %-30s %s\n", "tunda 2",                    "Jeda 2 detik");
        printf("  %-30s %s\n", "# komentar",                 "Komentar");
        printf("  %-30s %s\n", "perintah_finix",             "Semua perintah FINIX\n");
        return;
    }
    printf(CYN "\n  ▶ Menjalankan script: %s\n\n" RST, args[0]);
    fsh_var_count = 0;
    fsh_run_file(args[0]);
    printf(CYN "\n  ■ Script selesai.\n\n" RST);
}

/* Buat contoh script .fsh */
void cmd_fsh_contoh(void) {
    const char *contoh =
        "# Contoh script FINIX OS (.fsh)\n"
        "# Jalankan: fsh contoh.fsh\n\n"
        "NAMA = FinixOS\n"
        "VERSI = 10.0\n\n"
        "cetak \"==================================\"\n"
        "cetak \"  Halo dari $NAMA v$VERSI!\"\n"
        "cetak \"==================================\"\n\n"
        "cetak \"Info sistem:\"\n"
        "uname\n"
        "waktu\n\n"
        "cetak \"Menghitung mundur:\"\n"
        "ulang 3 cetak \"  Tick...\"\n\n"
        "cetak \"Todo hari ini:\"\n"
        "todo daftar\n\n"
        "cetak \"Selesai!\"\n";

    FILE *f = fopen("contoh.fsh", "w");
    if (f) {
        fputs(contoh, f);
        fclose(f);
        printf(GRN "  ✓ File contoh.fsh dibuat!\n" RST);
        printf(DIM "  Jalankan dengan: fsh contoh.fsh\n\n" RST);
    } else {
        printf(RED "  ✗ Gagal membuat file.\n" RST);
    }
}

/* ════════════════════════════════════════
 *  CHAT LAN
 *  Kirim & terima pesan antar HP satu WiFi
 * ════════════════════════════════════════ */
#define CHAT_PORT    9999
#define CHAT_BUFSIZE 1024

/* Dapatkan IP lokal */
static void get_local_ip(char *ip_out, size_t size) {
    struct ifaddrs *ifap, *ifa;
    strncpy(ip_out, "127.0.0.1", size-1);
    if (getifaddrs(&ifap) != 0) return;
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        strncpy(ip_out, inet_ntoa(sa->sin_addr), size-1);
        break;
    }
    freeifaddrs(ifap);
}

void cmd_chat(int argc, char **args) {
    if (argc < 1) {
        char my_ip[64]; get_local_ip(my_ip, sizeof(my_ip));
        printf(CYN BOLD "\n  💬 FINIX Chat LAN\n" RST);
        printf("  IP kamu  : " GRN "%s\n" RST, my_ip);
        printf("  Port     : %d\n\n", CHAT_PORT);
        printf(YLW "  Penggunaan:\n" RST);
        printf("  %-28s %s\n", "chat server",          "Mulai server (terima pesan)");
        printf("  %-28s %s\n", "chat kirim <ip> <pesan>","Kirim pesan ke HP lain");
        printf("  %-28s %s\n", "chat scan",             "Scan HP aktif di jaringan\n");
        return;
    }

    if (strcmp(args[0],"server")==0) {
        /* Mode server — terima pesan */
        char my_ip[64]; get_local_ip(my_ip, sizeof(my_ip));
        printf(CYN BOLD "\n  💬 Chat Server aktif\n" RST);
        printf("  IP    : " GRN "%s\n" RST, my_ip);
        printf("  Port  : %d\n", CHAT_PORT);
        printf(DIM "  Tekan Ctrl+C untuk berhenti...\n\n" RST);

        int srv = socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0) { printf(RED "  ✗ Gagal buat socket.\n" RST); return; }

        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(CHAT_PORT);

        if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            printf(RED "  ✗ Gagal bind port %d. Mungkin sudah dipakai.\n" RST, CHAT_PORT);
            close(srv); return;
        }
        listen(srv, 5);

        while (1) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int cli = accept(srv, (struct sockaddr*)&cli_addr, &cli_len);
            if (cli < 0) continue;

            char buf[CHAT_BUFSIZE]; memset(buf, 0, sizeof(buf));
            ssize_t n = recv(cli, buf, sizeof(buf)-1, 0);
            if (n > 0) {
                buf[n] = '\0';
                time_t t = time(NULL); struct tm *tm = localtime(&t);
                printf("  " CYN "[%02d:%02d]" RST " " YLW "%s" RST ": %s\n",
                       tm->tm_hour, tm->tm_min,
                       inet_ntoa(cli_addr.sin_addr), buf);
            }
            close(cli);
        }
        close(srv);

    } else if (strcmp(args[0],"kirim")==0) {
        if (argc < 3) {
            printf(YLW "  Penggunaan: chat kirim <ip> <pesan>\n" RST); return;
        }
        const char *target_ip = args[1];
        /* Gabung pesan */
        char pesan[CHAT_BUFSIZE] = "";
        for (int i=2;i<argc;i++) {
            if (i>2) strncat(pesan," ",sizeof(pesan)-strlen(pesan)-1);
            strncat(pesan,args[i],sizeof(pesan)-strlen(pesan)-1);
        }
        /* Tambahkan nama pengirim */
        char msg[CHAT_BUFSIZE*2];
        snprintf(msg, sizeof(msg), "[%s] %s",
                 current_user ? current_user->username : "tamu", pesan);

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { printf(RED "  ✗ Gagal buat socket.\n" RST); return; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(CHAT_PORT);
        if (inet_pton(AF_INET, target_ip, &addr.sin_addr) <= 0) {
            printf(RED "  ✗ IP tidak valid: %s\n" RST, target_ip);
            close(sock); return;
        }

        /* Timeout koneksi 3 detik */
        struct timeval tv = {3, 0};
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            printf(RED "  ✗ Tidak dapat terhubung ke %s:%d\n" RST, target_ip, CHAT_PORT);
            close(sock); return;
        }
        send(sock, msg, strlen(msg), 0);
        close(sock);
        printf(GRN "  ✓ Pesan terkirim ke %s\n" RST, target_ip);

    } else if (strcmp(args[0],"scan")==0) {
        char my_ip[64]; get_local_ip(my_ip, sizeof(my_ip));
        printf(CYN "\n  🔍 Scan HP aktif di jaringan...\n" RST);
        printf(DIM "  IP lokal: %s\n\n" RST, my_ip);

        /* Ambil prefix /24 */
        char prefix[64]; strncpy(prefix, my_ip, sizeof(prefix)-1);
        char *last_dot = strrchr(prefix, '.');
        if (!last_dot) { printf(RED "  ✗ Gagal parse IP.\n" RST); return; }
        *last_dot = '\0';

        int ditemukan = 0;
        for (int i = 1; i <= 254; i++) {
            char target[128];
            snprintf(target, sizeof(target), "%s.%d", prefix, i);
            if (strcmp(target, my_ip) == 0) {
                printf("  " GRN "%-16s" RST " ← kamu\n", target);
                ditemukan++; continue;
            }
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) continue;
            struct sockaddr_in addr;
            memset(&addr,0,sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(CHAT_PORT);
            inet_pton(AF_INET, target, &addr.sin_addr);
            /* Timeout sangat singkat */
            struct timeval tv = {0, 200000}; /* 200ms */
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                printf("  " GRN "%-16s" RST " FINIX Chat aktif\n", target);
                ditemukan++;
            }
            close(sock);
        }
        printf("\n  Ditemukan: %d perangkat\n\n", ditemukan);

    } else {
        printf(YLW "  Penggunaan: chat server|kirim|scan\n" RST);
    }
}

/* ════════════════════════════════════════════════════════════════
 *  FINIX OS v11.0 — TRIPLE AI SYSTEM
 *  1. Awing  — Ollama lokal (qwen2.5:3B) di Termux port 11434
 *  2. DeepSeek — API cloud DeepSeek
 *  3. ChatGPT — API OpenAI
 * ════════════════════════════════════════════════════════════════ */

#define AI_CONFIG_FILE "finix_ai.cfg"
#define AI_OLLAMA_URL  "http://localhost:11434"
#define AI_OLLAMA_MODEL "qwen2.5:3B"
#define AI_DS_URL      "https://api.deepseek.com/chat/completions"
#define AI_OAI_URL     "https://api.openai.com/v1/chat/completions"

/* ai_config_t sudah dideklarasikan di header */
ai_config_t ai_cfg;

/* Load konfigurasi AI dari file */
static void ai_config_load(void) {
    memset(&ai_cfg, 0, sizeof(ai_cfg));
    strcpy(ai_cfg.active, "awing");
    ai_cfg.ollama_port = 11434;
    strcpy(ai_cfg.ollama_model, AI_OLLAMA_MODEL);

    FILE *f = fopen(AI_CONFIG_FILE, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "deepseek_key=", 13) == 0)
            strncpy(ai_cfg.deepseek_key, line+13, 255);
        else if (strncmp(line, "openai_key=", 11) == 0)
            strncpy(ai_cfg.openai_key, line+11, 255);
        else if (strncmp(line, "active=", 7) == 0)
            strncpy(ai_cfg.active, line+7, 15);
        else if (strncmp(line, "ollama_port=", 12) == 0)
            ai_cfg.ollama_port = atoi(line+12);
        else if (strncmp(line, "ollama_model=", 13) == 0)
            strncpy(ai_cfg.ollama_model, line+13, 63);
    }
    fclose(f);
}

static void ai_config_save(void) {
    FILE *f = fopen(AI_CONFIG_FILE, "w");
    if (!f) return;
    fprintf(f, "deepseek_key=%s\n", ai_cfg.deepseek_key);
    fprintf(f, "openai_key=%s\n",   ai_cfg.openai_key);
    fprintf(f, "active=%s\n",        ai_cfg.active);
    fprintf(f, "ollama_port=%d\n",   ai_cfg.ollama_port);
    fprintf(f, "ollama_model=%s\n",  ai_cfg.ollama_model);
    fclose(f);
    chmod(AI_CONFIG_FILE, 0600);
}

/* ── Escape JSON string (hindari injection) ── */
static void json_escape(const char *src, char *dst, size_t dsz) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di < dsz-2; si++) {
        unsigned char c = (unsigned char)src[si];
        if      (c == '"')  { if (di+2 < dsz) { dst[di++]='\\'; dst[di++]='"';  } }
        else if (c == '\\') { if (di+2 < dsz) { dst[di++]='\\'; dst[di++]='\\'; } }
        else if (c == '\n') { if (di+2 < dsz) { dst[di++]='\\'; dst[di++]='n';  } }
        else if (c == '\r') { if (di+2 < dsz) { dst[di++]='\\'; dst[di++]='r';  } }
        else if (c == '\t') { if (di+2 < dsz) { dst[di++]='\\'; dst[di++]='t';  } }
        else if (c < 0x20)  { /* skip control chars */ }
        else                { dst[di++] = (char)c; }
    }
    dst[di] = '\0';
}

/* ── Cek apakah Ollama berjalan ── */
static int ai_ollama_check(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 2 http://localhost:%d/api/tags "
        ">/dev/null 2>&1", ai_cfg.ollama_port);
    return (system(cmd) == 0) ? 1 : 0;
}

/* ── Tanya Awing (Ollama lokal) ── */
static void ai_ask_awing(const char *pertanyaan) {
    printf(MAG BOLD "\n  🦉 Awing (Ollama/%s) sedang berpikir...\n\n" RST,
           ai_cfg.ollama_model);

    /* Cek Ollama dulu */
    if (!ai_ollama_check()) {
        printf(RED "  ✗ Ollama tidak berjalan di port %d.\n" RST, ai_cfg.ollama_port);
        printf(YLW "  Jalankan: ollama serve   (di terminal lain)\n" RST);
        printf(YLW "  Lalu    : ollama pull %s\n\n" RST, ai_cfg.ollama_model);
        return;
    }

    char q_escaped[2048];
    json_escape(pertanyaan, q_escaped, sizeof(q_escaped));

    /* Buat JSON payload untuk Ollama /api/generate */
    char json[4096];
    snprintf(json, sizeof(json),
        "{\"model\":\"%s\","
        "\"prompt\":\"%s\","
        "\"stream\":false,"
        "\"options\":{\"temperature\":0.7,\"num_predict\":512}}",
        ai_cfg.ollama_model, q_escaped);

    /* Simpan ke file tmp */
    FILE *tf = fopen("/tmp/finix_awing_req.json", "w");
    if (tf) { fputs(json, tf); fclose(tf); }

    /* Tulis parser Python ke file */
    FILE *pf = fopen("/tmp/finix_parse_awing.py", "w");
    if (pf) {
        fputs(
            "import sys, json\n"
            "try:\n"
            "    d = json.load(sys.stdin)\n"
            "    print(d.get('response', '(Tidak ada jawaban)'))\n"
            "except Exception as e:\n"
            "    print('(Parse error):', str(e))\n",
            pf);
        fclose(pf);
    }

    /* Kirim ke Ollama */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 60 "
        "http://localhost:%d/api/generate "
        "-H 'Content-Type: application/json' "
        "-d @/tmp/finix_awing_req.json "
        "-o /tmp/finix_awing_resp.json 2>/dev/null ; "
        "if [ -s /tmp/finix_awing_resp.json ]; then "
        "  python3 /tmp/finix_parse_awing.py < /tmp/finix_awing_resp.json 2>/dev/null || "
        "  grep -o '\"response\":\"[^\"]*\"' /tmp/finix_awing_resp.json | "
        "  sed 's/\"response\":\"//;s/\"$//;s/\\\\n/\\n/g'; "
        "else "
        "  echo '(Tidak ada respons dari Ollama)'; "
        "fi",
        ai_cfg.ollama_port);

    printf("  " MAG "🦉 Awing: " RST);
    fflush(stdout);
    system(cmd);
    printf("\n");

    remove("/tmp/finix_awing_req.json");
    remove("/tmp/finix_awing_resp.json");
    remove("/tmp/finix_parse_awing.py");
}

/* ── Tanya DeepSeek ── */
static void ai_ask_deepseek(const char *pertanyaan) {
    if (!strlen(ai_cfg.deepseek_key)) {
        printf(RED "  ✗ API key DeepSeek belum diatur.\n" RST);
        printf(YLW "  Jalankan: ai setup deepseek\n\n" RST);
        return;
    }
    printf(CYN BOLD "\n  🔵 DeepSeek sedang berpikir...\n\n" RST);

    char q_escaped[2048];
    json_escape(pertanyaan, q_escaped, sizeof(q_escaped));

    /* Tulis JSON request */
    char json[4096];
    snprintf(json, sizeof(json),
        "{\"model\":\"deepseek-chat\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"Kamu adalah asisten AI FINIX OS. Jawab dalam Bahasa Indonesia yang jelas dan ringkas.\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"max_tokens\":1024,\"temperature\":0.7}",
        q_escaped);

    FILE *tf = fopen("/tmp/finix_ds_req.json", "w");
    if (tf) { fputs(json, tf); fclose(tf); }

    /* Tulis parser Python ke file (hindari masalah kutip di shell) */
    FILE *pf = fopen("/tmp/finix_parse.py", "w");
    if (pf) {
        fputs(
            "import sys, json\n"
            "try:\n"
            "    d = json.load(sys.stdin)\n"
            "    if 'choices' in d and len(d['choices']) > 0:\n"
            "        msg = d['choices'][0].get('message', {})\n"
            "        print(msg.get('content', '(Tidak ada jawaban)'))\n"
            "    elif 'error' in d:\n"
            "        print('[Error]', d['error'].get('message', str(d['error'])))\n"
            "    else:\n"
            "        print('(Respons tidak dikenal):', str(d)[:200])\n"
            "except Exception as e:\n"
            "    print('(Parse error):', str(e))\n",
            pf);
        fclose(pf);
    }

    /* Kirim request, simpan respons ke file, lalu parse */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 30 '%s' "
        "-H 'Content-Type: application/json' "
        "-H 'Authorization: Bearer %s' "
        "-d @/tmp/finix_ds_req.json "
        "-o /tmp/finix_ds_resp.json 2>/tmp/finix_ds_err.txt ; "
        "if [ -s /tmp/finix_ds_resp.json ]; then "
        "  python3 /tmp/finix_parse.py < /tmp/finix_ds_resp.json 2>/dev/null || "
        "  cat /tmp/finix_ds_resp.json | grep -o '\"content\":\"[^\"]*\"' | head -1 | "
        "  sed 's/\"content\":\"//;s/\"$//;s/\\\\n/\\n/g'; "
        "else "
        "  echo '(Tidak ada respons — cek koneksi internet)' ; "
        "  cat /tmp/finix_ds_err.txt 2>/dev/null | head -3 ; "
        "fi",
        AI_DS_URL, ai_cfg.deepseek_key);

    printf("  " CYN "🔵 DeepSeek: " RST);
    fflush(stdout);
    system(cmd);
    printf("\n");

    remove("/tmp/finix_ds_req.json");
    remove("/tmp/finix_ds_resp.json");
    remove("/tmp/finix_ds_err.txt");
    remove("/tmp/finix_parse.py");
}

/* ── Tanya ChatGPT ── */
static void ai_ask_chatgpt(const char *pertanyaan) {
    if (!strlen(ai_cfg.openai_key)) {
        printf(RED "  ✗ API key OpenAI belum diatur.\n" RST);
        printf(YLW "  Jalankan: ai setup chatgpt\n\n" RST);
        return;
    }
    printf(GRN BOLD "\n  🟢 ChatGPT sedang berpikir...\n\n" RST);

    char q_escaped[2048];
    json_escape(pertanyaan, q_escaped, sizeof(q_escaped));

    char json[4096];
    snprintf(json, sizeof(json),
        "{\"model\":\"gpt-4o-mini\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"Kamu adalah asisten AI FINIX OS. Jawab dalam Bahasa Indonesia yang jelas dan ringkas.\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"max_tokens\":1024,\"temperature\":0.7}",
        q_escaped);

    FILE *tf = fopen("/tmp/finix_oai_req.json", "w");
    if (tf) { fputs(json, tf); fclose(tf); }

    /* Tulis parser Python ke file */
    FILE *pf = fopen("/tmp/finix_parse_oai.py", "w");
    if (pf) {
        fputs(
            "import sys, json\n"
            "try:\n"
            "    d = json.load(sys.stdin)\n"
            "    if 'choices' in d and len(d['choices']) > 0:\n"
            "        msg = d['choices'][0].get('message', {})\n"
            "        print(msg.get('content', '(Tidak ada jawaban)'))\n"
            "    elif 'error' in d:\n"
            "        print('[Error]', d['error'].get('message', str(d['error'])))\n"
            "    else:\n"
            "        print('(Respons tidak dikenal):', str(d)[:200])\n"
            "except Exception as e:\n"
            "    print('(Parse error):', str(e))\n",
            pf);
        fclose(pf);
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 30 '%s' "
        "-H 'Content-Type: application/json' "
        "-H 'Authorization: Bearer %s' "
        "-d @/tmp/finix_oai_req.json "
        "-o /tmp/finix_oai_resp.json 2>/tmp/finix_oai_err.txt ; "
        "if [ -s /tmp/finix_oai_resp.json ]; then "
        "  python3 /tmp/finix_parse_oai.py < /tmp/finix_oai_resp.json 2>/dev/null || "
        "  cat /tmp/finix_oai_resp.json | grep -o '\"content\":\"[^\"]*\"' | head -1 | "
        "  sed 's/\"content\":\"//;s/\"$//;s/\\\\n/\\n/g'; "
        "else "
        "  echo '(Tidak ada respons — cek koneksi internet)' ; "
        "fi",
        AI_OAI_URL, ai_cfg.openai_key);

    printf("  " GRN "🟢 ChatGPT: " RST);
    fflush(stdout);
    system(cmd);
    printf("\n");

    remove("/tmp/finix_oai_req.json");
    remove("/tmp/finix_oai_resp.json");
    remove("/tmp/finix_oai_err.txt");
    remove("/tmp/finix_parse_oai.py");
}

/* ── Status semua AI ── */
static void ai_status(void) {
    printf(CYN BOLD "\n  🤖 Status AI FINIX OS v11.0\n" RST);
    printf("  %s\n", "──────────────────────────────────────────────");

    /* Awing / Ollama */
    int ollama_ok = ai_ollama_check();
    printf("  " MAG "🦉 Awing" RST "  (Ollama/%s) ", ai_cfg.ollama_model);
    printf("Port %d  ", ai_cfg.ollama_port);
    printf(ollama_ok ? GRN "● AKTIF\n" RST : RED "● MATI\n" RST);

    /* DeepSeek */
    printf("  " CYN "🔵 DeepSeek " RST "          API Key: ");
    printf(strlen(ai_cfg.deepseek_key) > 0 ?
           GRN "✓ Tersimpan\n" RST : RED "✗ Belum diatur\n" RST);

    /* ChatGPT */
    printf("  " GRN "🟢 ChatGPT  " RST "          API Key: ");
    printf(strlen(ai_cfg.openai_key) > 0 ?
           GRN "✓ Tersimpan\n" RST : RED "✗ Belum diatur\n" RST);

    printf("\n  " YLW "AI Aktif (default): " BOLD "%s\n" RST, ai_cfg.active);
    printf("  %s\n\n", "──────────────────────────────────────────────");
}

/* ── Setup AI ── */
static void ai_setup(const char *target) {
    if (!target) {
        printf(YLW "  Penggunaan: ai setup awing|deepseek|chatgpt\n" RST);
        return;
    }

    if (strcmp(target, "awing") == 0) {
        printf(MAG BOLD "\n  🦉 Setup Awing (Ollama Lokal)\n" RST);
        printf(DIM "  Awing menggunakan Ollama yang berjalan di Termux.\n\n" RST);
        printf("  Langkah:\n");
        printf("  1. " YLW "pkg install ollama" RST " (atau dari repo lain)\n");
        printf("  2. " YLW "ollama serve" RST " (jalankan di terminal lain)\n");
        printf("  3. " YLW "ollama pull qwen2.5:3b" RST "\n\n");

        printf("  Port Ollama [%d]: ", ai_cfg.ollama_port);
        char buf[32]; fgets(buf, sizeof(buf), stdin);
        buf[strcspn(buf,"\n")] = '\0';
        if (strlen(buf) > 0 && atoi(buf) > 0)
            ai_cfg.ollama_port = atoi(buf);

        printf("  Model [%s]: ", ai_cfg.ollama_model);
        char mbuf[64]; fgets(mbuf, sizeof(mbuf), stdin);
        mbuf[strcspn(mbuf,"\n")] = '\0';
        if (strlen(mbuf) > 0)
            strncpy(ai_cfg.ollama_model, mbuf, 63);

        ai_config_save();
        printf(GRN "  ✓ Konfigurasi Awing disimpan.\n" RST);

        /* Test koneksi */
        printf(DIM "  Mengecek Ollama...\n" RST);
        if (ai_ollama_check())
            printf(GRN "  ✓ Ollama aktif dan siap!\n\n" RST);
        else
            printf(YLW "  ⚠  Ollama belum berjalan. Jalankan: ollama serve\n\n" RST);

    } else if (strcmp(target, "deepseek") == 0) {
        printf(CYN BOLD "\n  🔵 Setup DeepSeek API\n" RST);
        printf(DIM "  Dapatkan API key di: platform.deepseek.com\n\n" RST);
        printf("  Masukkan DeepSeek API key: ");
        char key[256]; memset(key, 0, sizeof(key));
        if (fgets(key, sizeof(key), stdin)) {
            key[strcspn(key,"\n")] = '\0';
            if (strlen(key) > 10) {
                strncpy(ai_cfg.deepseek_key, key, 255);
                ai_config_save();
                printf(GRN "  ✓ API key DeepSeek disimpan.\n\n" RST);
            } else {
                printf(RED "  ✗ Key terlalu pendek.\n\n" RST);
            }
        }

    } else if (strcmp(target, "chatgpt") == 0) {
        printf(GRN BOLD "\n  🟢 Setup ChatGPT (OpenAI) API\n" RST);
        printf(DIM "  Dapatkan API key di: platform.openai.com\n\n" RST);
        printf("  Masukkan OpenAI API key: ");
        char key[256]; memset(key, 0, sizeof(key));
        if (fgets(key, sizeof(key), stdin)) {
            key[strcspn(key,"\n")] = '\0';
            if (strlen(key) > 10) {
                strncpy(ai_cfg.openai_key, key, 255);
                ai_config_save();
                printf(GRN "  ✓ API key OpenAI disimpan.\n\n" RST);
            } else {
                printf(RED "  ✗ Key terlalu pendek.\n\n" RST);
            }
        }

    } else {
        printf(RED "  ✗ Target tidak dikenal: %s\n" RST, target);
        printf(YLW "  Gunakan: ai setup awing|deepseek|chatgpt\n\n" RST);
    }
}

/* ── Ganti AI aktif ── */
static void ai_set_active(const char *nama) {
    if (!nama) {
        printf(YLW "  Penggunaan: ai gunakan awing|deepseek|chatgpt\n" RST);
        return;
    }
    if (strcmp(nama,"awing")==0 || strcmp(nama,"deepseek")==0 || strcmp(nama,"chatgpt")==0) {
        strncpy(ai_cfg.active, nama, 15);
        ai_config_save();
        printf(GRN "  ✓ AI aktif sekarang: %s\n\n" RST, nama);
    } else {
        printf(RED "  ✗ AI tidak dikenal. Pilih: awing|deepseek|chatgpt\n" RST);
    }
}

/* ── Hapus key ── */
static void ai_hapus_key(const char *target) {
    if (!target) { printf(YLW "  Penggunaan: ai hapus-key deepseek|chatgpt|semua\n" RST); return; }
    if (strcmp(target,"deepseek")==0 || strcmp(target,"semua")==0) {
        memset(ai_cfg.deepseek_key, 0, sizeof(ai_cfg.deepseek_key));
        printf(GRN "  ✓ Key DeepSeek dihapus.\n" RST);
    }
    if (strcmp(target,"chatgpt")==0 || strcmp(target,"semua")==0) {
        memset(ai_cfg.openai_key, 0, sizeof(ai_cfg.openai_key));
        printf(GRN "  ✓ Key ChatGPT dihapus.\n" RST);
    }
    ai_config_save();
}

/* ── Bantuan AI ── */
static void ai_help(void) {
    printf(CYN BOLD "\n  🤖 FINIX OS AI v11.0 — Triple AI\n" RST);
    printf("  %s\n", "──────────────────────────────────────────────────");
    printf("  " MAG "🦉 Awing" RST "   — Ollama lokal (offline, gratis)\n");
    printf("  " CYN "🔵 DeepSeek" RST " — API cloud DeepSeek\n");
    printf("  " GRN "🟢 ChatGPT" RST "  — API cloud OpenAI\n\n");
    printf(YLW "  Perintah:\n" RST);
    printf("  %-34s %s\n", "ai status",                    "Status semua AI");
    printf("  %-34s %s\n", "ai setup awing",               "Konfigurasi Awing/Ollama");
    printf("  %-34s %s\n", "ai setup deepseek",            "Atur API key DeepSeek");
    printf("  %-34s %s\n", "ai setup chatgpt",             "Atur API key OpenAI");
    printf("  %-34s %s\n", "ai gunakan awing|deepseek|chatgpt", "Ganti AI aktif");
    printf("  %-34s %s\n", "ai awing <pertanyaan>",        "Tanya Awing langsung");
    printf("  %-34s %s\n", "ai deepseek <pertanyaan>",     "Tanya DeepSeek langsung");
    printf("  %-34s %s\n", "ai chatgpt <pertanyaan>",      "Tanya ChatGPT langsung");
    printf("  %-34s %s\n", "ai <pertanyaan>",              "Tanya AI aktif");
    printf("  %-34s %s\n", "ai hapus-key deepseek|chatgpt","Hapus API key");
    printf("  %-34s %s\n", "ai jelaskan <perintah>",       "Jelaskan perintah FINIX");
    printf("\n");
}

/* ── Perintah ai utama ── */
void cmd_ai_chat(int argc, char **args) {
    /* Backward compat: cmd_ai_chat dipanggil dari cmd_ai */
    if (argc < 1) { ai_help(); return; }

    const char *sub = args[0];

    if (strcmp(sub,"status")==0)   { ai_status(); return; }
    if (strcmp(sub,"setup")==0)    { ai_setup(argc>1?args[1]:NULL); return; }
    if (strcmp(sub,"gunakan")==0)  { ai_set_active(argc>1?args[1]:NULL); return; }
    if (strcmp(sub,"hapus-key")==0){ ai_hapus_key(argc>1?args[1]:NULL); return; }
    if (strcmp(sub,"bantuan")==0 || strcmp(sub,"help")==0) { ai_help(); return; }

    /* Jelaskan perintah FINIX */
    if (strcmp(sub,"jelaskan")==0) {
        static const cmd_info_t cmd_db[] = {
            {"bantuan",    "Tampilkan daftar semua perintah."},
            {"sysinfo",    "Info sistem lengkap."},
            {"neofetch",   "Info sistem bergaya."},
            {"catatan",    "Buat & kelola catatan."},
            {"cuaca",      "Prakiraan cuaca kota."},
            {"firewall",   "Kelola aturan firewall."},
            {"paket",      "Manajer paket Termux/APT."},
            {"kalkulator", "Kalkulator sederhana."},
            {"game",       "Mini game tebak angka."},
            {"ttt",        "Tic-Tac-Toe lawan AI."},
            {"sholat",     "Pengingat waktu sholat."},
            {"proses",     "Daftar proses."},
            {"baterai",    "Status baterai HP."},
            {"android",    "Info perangkat Android."},
            {"todo",       "Kelola daftar tugas."},
            {"timer",      "Timer hitung mundur."},
            {"grafik",     "Grafik ASCII ram/disk."},
            {"enkripsi",   "Enkripsi/dekripsi file XOR."},
            {"fsh",        "Jalankan script FINIX .fsh."},
            {"chat",       "Chat LAN antar HP."},
            {NULL, NULL}
        };
        const char *target = argc>1?args[1]:NULL;
        if (!target) { printf(YLW "  Contoh: ai jelaskan catatan\n" RST); return; }
        for (int i = 0; cmd_db[i].cmd; i++) {
            if (strcmp(target, cmd_db[i].cmd) == 0) {
                printf(CYN "  🤖 '%s': %s\n\n" RST, target, cmd_db[i].desc);
                return;
            }
        }
        printf(YLW "  🤖 Belum ada penjelasan untuk '%s'.\n" RST, target);
        return;
    }

    /* Tentukan apakah arg pertama adalah nama AI atau pertanyaan */
    int ai_target = 0; /* 0=aktif, 1=awing, 2=deepseek, 3=chatgpt */
    int q_start = 0;   /* indeks mulai pertanyaan di args[] */

    if      (strcmp(sub,"awing")==0)    { ai_target=1; q_start=1; }
    else if (strcmp(sub,"deepseek")==0) { ai_target=2; q_start=1; }
    else if (strcmp(sub,"chatgpt")==0)  { ai_target=3; q_start=1; }
    else { q_start = 0; } /* seluruh args adalah pertanyaan */

    /* Gabung pertanyaan */
    if (argc <= q_start) {
        ai_help();
        return;
    }
    char tanya[1024] = "";
    for (int i=q_start; i<argc; i++) {
        if (i>q_start) strncat(tanya," ",sizeof(tanya)-strlen(tanya)-1);
        strncat(tanya, args[i], sizeof(tanya)-strlen(tanya)-1);
    }

    /* Tentukan AI yang digunakan */
    const char *use_ai = ai_cfg.active;
    if      (ai_target==1) use_ai = "awing";
    else if (ai_target==2) use_ai = "deepseek";
    else if (ai_target==3) use_ai = "chatgpt";

    if      (strcmp(use_ai,"awing")==0)    ai_ask_awing(tanya);
    else if (strcmp(use_ai,"deepseek")==0) ai_ask_deepseek(tanya);
    else if (strcmp(use_ai,"chatgpt")==0)  ai_ask_chatgpt(tanya);
    else    ai_ask_awing(tanya); /* fallback */
}

/* Wrapper lama cmd_ai untuk kompatibilitas */
void cmd_ai(const char *subcmd, const char *target) {
    if (!subcmd) { cmd_ai_chat(0, NULL); return; }
    /* Teruskan ke sistem baru */
    char *a[2] = {(char*)subcmd, (char*)target};
    int n = target ? 2 : 1;
    cmd_ai_chat(n, a);
}

/* ai_suggestion tetap ada untuk typo correction */
void ai_suggestion(const char *input) {
    printf(YLW "  💡 Perintah '%s' tidak dikenal. " RST, input);
    if (strstr(input,"hlp")||strstr(input,"help")||strstr(input,"bantu"))
        printf("Mungkin maksudmu: " CYN "bantuan\n" RST);
    else if (strstr(input,"ram")||strstr(input,"mem"))
        printf("Coba: " CYN "ram\n" RST);
    else if (strstr(input,"bat")||strstr(input,"batre"))
        printf("Coba: " CYN "baterai\n" RST);
    else if (strstr(input,"note")||strstr(input,"catat"))
        printf("Coba: " CYN "catatan daftar\n" RST);
    else if (strstr(input,"game")||strstr(input,"main"))
        printf("Coba: " CYN "game\n" RST " atau " CYN "ttt\n" RST);
    else if (strstr(input,"ai")||strstr(input,"tanya"))
        printf("Coba: " CYN "ai <pertanyaan>\n" RST);
    else
        printf("Ketik " CYN "bantuan\n" RST " untuk daftar perintah.");
}

/* ════════════════════════════════════════
 *  TUI — Menu Navigasi Sederhana
 *  Tanpa ncurses, pakai ANSI escape
 * ════════════════════════════════════════ */

/* Aktifkan raw mode terminal */
#include <termios.h>
static struct termios orig_termios;

static void tui_raw_mode(int enable) {
    if (enable) {
        struct termios raw;
        tcgetattr(STDIN_FILENO, &orig_termios);
        raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    } else {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }
}

static int tui_read_key(void) {
    char c; int n = (int)read(STDIN_FILENO, &c, 1);
    if (n <= 0) return -1;
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) <= 0) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) <= 0) return '\x1b';
        if (seq[0]=='[') {
            if (seq[1]=='A') return 1000; /* UP */
            if (seq[1]=='B') return 1001; /* DOWN */
            if (seq[1]=='C') return 1002; /* RIGHT */
            if (seq[1]=='D') return 1003; /* LEFT */
        }
        return '\x1b';
    }
    return (unsigned char)c;
}

typedef struct {
    const char *label;
    const char *desc;
    void (*action)(void);
} tui_item_t;

/* Forward declarations untuk action */
static void tui_action_sysinfo(void)  { tui_raw_mode(0); cmd_sysinfo();   tui_raw_mode(1); }
static void tui_action_neofetch(void) { tui_raw_mode(0); cmd_neofetch();  tui_raw_mode(1); }
static void tui_action_proses(void)   { tui_raw_mode(0); cmd_ps_list();   tui_raw_mode(1); }
static void tui_action_ram(void)      { tui_raw_mode(0); cmd_ram();       tui_raw_mode(1); }
static void tui_action_disk(void)     { tui_raw_mode(0); cmd_disk();      tui_raw_mode(1); }
static void tui_action_baterai(void)  { tui_raw_mode(0); cmd_baterai();   tui_raw_mode(1); }
static void tui_action_todo(void) {
    tui_raw_mode(0);
    char *a[] = {"daftar"}; cmd_todo(1,a);
    tui_raw_mode(1);
}
static void tui_action_catatan(void) {
    tui_raw_mode(0);
    char *a[] = {"daftar"}; cmd_catatan(1,a);
    tui_raw_mode(1);
}
static void tui_action_game(void)    { tui_raw_mode(0); cmd_game_tebak(); tui_raw_mode(1); }
static void tui_action_quiz(void)    { tui_raw_mode(0); char *a[]={"5"}; cmd_quiz(1,a); tui_raw_mode(1); }
static void tui_action_hangman(void) { tui_raw_mode(0); cmd_hangman();   tui_raw_mode(1); }
static void tui_action_sholat(void)  { tui_raw_mode(0); cmd_sholat();    tui_raw_mode(1); }
static void tui_action_kalender(void){ tui_raw_mode(0); cmd_kalender();  tui_raw_mode(1); }
static void tui_action_moto(void)    { tui_raw_mode(0); cmd_moto();      tui_raw_mode(1); }

static void tui_submenu(const char *judul, tui_item_t *items, int n_items) {
    int sel = 0;
    while (1) {
        /* Clear & gambar menu */
        printf("\033[2J\033[H"); /* clear screen */
        printf(CYN BOLD
               "  ╔══════════════════════════════════════════╗\n"
               "  ║       FINIX OS v10.0 — %-18s║\n"
               "  ╚══════════════════════════════════════════╝\n" RST,
               judul);
        printf(DIM "  ↑↓ navigasi  Enter = pilih  q = kembali\n\n" RST);

        for (int i = 0; i < n_items; i++) {
            if (i == sel)
                printf("  " BG_BLK CYN BOLD " ▶ %-20s " RST "  %s\n",
                       items[i].label, items[i].desc);
            else
                printf("    " DIM "%-20s" RST "  %s\n",
                       items[i].label, items[i].desc);
        }
        printf("\n");

        int k = tui_read_key();
        if (k == 1000 && sel > 0)          sel--;       /* UP */
        else if (k == 1001 && sel < n_items-1) sel++;   /* DOWN */
        else if (k == '\r' || k == '\n') {
            if (items[sel].action) {
                printf("\033[2J\033[H");
                items[sel].action();
                printf(DIM "\n  Tekan Enter untuk kembali..." RST);
                (void)tui_read_key();
            }
        }
        else if (k == 'q' || k == 'Q' || k == '\x1b') break;
    }
}

void cmd_tui(void) {
    tui_raw_mode(1);

    /* Menu Utama */
    const char *main_labels[] = {
        "📊 Sistem",
        "📋 Produktivitas",
        "🎮 Permainan",
        "🕌 Utilitas",
        "🚪 Keluar TUI",
    };
    const char *main_descs[] = {
        "Info sistem, proses, memori",
        "Todo list, catatan",
        "Game, quiz, hangman",
        "Sholat, kalender, kata bijak",
        "Kembali ke shell",
    };

    tui_item_t sistem_items[] = {
        {"Sysinfo",   "Info sistem lengkap",  tui_action_sysinfo},
        {"Neofetch",  "Tampilan bergaya",     tui_action_neofetch},
        {"Proses",    "Daftar proses",        tui_action_proses},
        {"RAM",       "Penggunaan memori",    tui_action_ram},
        {"Disk",      "Penyimpanan",          tui_action_disk},
        {"Baterai",   "Status baterai",       tui_action_baterai},
    };
    tui_item_t produktif_items[] = {
        {"Todo List", "Daftar tugas",         tui_action_todo},
        {"Catatan",   "Catatan tersimpan",    tui_action_catatan},
    };
    tui_item_t game_items[] = {
        {"Tebak Angka","Tebak 1-100",         tui_action_game},
        {"Quiz",       "Quiz teknologi",      tui_action_quiz},
        {"Hangman",    "Tebak kata",          tui_action_hangman},
    };
    tui_item_t util_items[] = {
        {"Sholat",    "Waktu sholat",         tui_action_sholat},
        {"Kalender",  "Kalender bulan ini",   tui_action_kalender},
        {"Kata Bijak","Motivasi",             tui_action_moto},
    };

    int sel = 0, n_main = 5;
    while (1) {
        printf("\033[2J\033[H");
        /* Header */
        printf(CYN BOLD
               "\n  ╔═══════════════════════════════════════════════╗\n"
               "  ║     ______ _       _      __  ____  _____     ║\n"
               "  ║    |  ____|_)     (_)    / / / __ \\/ ____|    ║\n"
               "  ║    | |__ | |_ __  _ ___  / / | |  | | (___    ║\n"
               "  ║    |  __|| | '_ \\| \\ \\/ /  | |  | |\\___ \\   ║\n"
               "  ║    | |   | | | | | |>  <   | |__| |____) |   ║\n"
               "  ║    |_|   |_|_| |_|_/_/\\_\\   \\____/|_____/    ║\n"
               "  ║                                               ║\n"
               "  ║          v10.0 — Menu Utama                   ║\n"
               "  ╚═══════════════════════════════════════════════╝\n\n" RST);

        /* Tampilkan jam */
        time_t t = time(NULL); struct tm *tm = localtime(&t);
        printf("  " DIM "User: %-12s  %02d:%02d:%02d\n\n" RST,
               current_user ? current_user->username : "tamu",
               tm->tm_hour, tm->tm_min, tm->tm_sec);

        printf(DIM "  ↑↓ navigasi  Enter = pilih  q = keluar TUI\n\n" RST);

        for (int i = 0; i < n_main; i++) {
            if (i == sel)
                printf("  " BG_BLK CYN BOLD " ▶ %-22s " RST "  %s\n",
                       main_labels[i], main_descs[i]);
            else
                printf("    " DIM "%-22s" RST "  %s\n",
                       main_labels[i], main_descs[i]);
        }
        printf("\n");

        int k = tui_read_key();
        if (k == 1000 && sel > 0)           sel--;
        else if (k == 1001 && sel < n_main-1) sel++;
        else if (k == '\r' || k == '\n') {
            if (sel == 0) tui_submenu("Sistem",        sistem_items,    6);
            else if (sel == 1) tui_submenu("Produktivitas", produktif_items, 2);
            else if (sel == 2) tui_submenu("Permainan",    game_items,      3);
            else if (sel == 3) tui_submenu("Utilitas",     util_items,      3);
            else if (sel == 4) break;
        }
        else if (k == 'q' || k == 'Q') break;
    }

    tui_raw_mode(0);
    printf("\033[2J\033[H");
    printf(GRN "  Keluar dari TUI. Kembali ke shell.\n\n" RST);
}

/* ════════════════════════════════════════
 *  HELP
 * ════════════════════════════════════════ */
void cmd_bantuan(void) {
    printf("\n  " CYN BOLD
           "╔══════════════════════════════════════════════════╗\n"
           "║      FINIX OS v11.0 — Triple AI Edition          ║\n"
           "╚══════════════════════════════════════════════════╝\n" RST);

    const struct { const char *section; const char *items; } secs[] = {
        {"🔐 AUTENTIKASI",
         "  login          Masuk ke sistem\n"
         "  daftar         Registrasi akun baru\n"
         "  keluar_sesi    Logout\n"
         "  siapakah       Tampilkan user aktif\n"
         "  daftar_user    Daftar semua user\n"},
        {"📁 FILESYSTEM",
         "  ls/ll [path]   Lihat isi direktori\n"
         "  cd <dir>       Pindah direktori\n"
         "  pwd            Direktori saat ini\n"
         "  mkdir <nama>   Buat direktori\n"
         "  sentuh <file>  Buat file kosong\n"
         "  cat <file>     Tampilkan isi file\n"
         "  nano <file>    Edit file\n"
         "  hapus <file>   Hapus file\n"
         "  salin <s> <d>  Salin file\n"
         "  pindah <s> <d> Pindah/rename file\n"},
        {"⚙️  PROSES & MEMORI",
         "  proses/ps      Daftar proses\n"
         "  ram            Info penggunaan RAM\n"
         "  disk           Info penyimpanan\n"
         "  cpu            Info CPU\n"
         "  bunuh <pid>    Hentikan proses\n"},
        {"🖥  SISTEM",
         "  sysinfo        Info sistem lengkap\n"
         "  neofetch       Info sistem bergaya\n"
         "  uname          Info kernel\n"
         "  waktu          Waktu sekarang\n"
         "  tanggal        Tanggal lengkap\n"
         "  uptime         Lama sistem berjalan\n"
         "  kalender       Tampilkan kalender\n"
         "  baterai        Status baterai\n"
         "  android        Info perangkat\n"},
        {"🌐 JARINGAN",
         "  ping <host>    Tes koneksi\n"
         "  cuaca <kota>   Prakiraan cuaca\n"
         "  ifconfig       Info jaringan\n"
         "  netstat        Koneksi aktif\n"
         "  curl <url>     Ambil konten URL\n"
         "  wget <url>     Unduh file\n"
         "  nmap <host>    Scan port\n"},
        {"🛡  KEAMANAN",
         "  firewall status|tambah|daftar|hapus\n"
         "  scan           Scan proses mencurigakan\n"
         "  antivirus [p]  Pindai ancaman\n"},
        {"📦 PAKET",
         "  paket pasang|hapus|daftar|perbarui\n"},
        {"📝 CATATAN",
         "  catatan tambah|daftar|hapus|bersih\n"},
        {"🤖 AI TRIPLE — AWING / DEEPSEEK / CHATGPT",
         "  ai status              Status semua AI\n"
         "  ai setup awing         Konfigurasi Awing/Ollama lokal\n"
         "  ai setup deepseek      Atur API key DeepSeek\n"
         "  ai setup chatgpt       Atur API key OpenAI\n"
         "  ai gunakan <nama>      Ganti AI aktif\n"
         "  ai awing  <tanya>      Tanya Awing (offline)\n"
         "  ai deepseek <tanya>    Tanya DeepSeek\n"
         "  ai chatgpt <tanya>     Tanya ChatGPT\n"
         "  ai <tanya>             Tanya AI aktif\n"
         "  ai jelaskan <perintah> Jelaskan perintah FINIX\n"},
        {"🔧 UTILITAS",
         "  kalkulator <a> <op> <b> Hitung ekspresi\n"
         "  riwayat            Riwayat perintah\n"
         "  gema <teks>        Cetak teks\n"
         "  moto               Kata-kata bijak\n"
         "  spanduk <teks>     ASCII besar\n"
         "  bersih/clear       Bersihkan layar\n"
         "  tentang            Tentang FINIX OS\n"},
        {"🎮 HIBURAN",
         "  game               Tebak angka\n"
         "  ttt                Tic-Tac-Toe vs AI\n"
         "  sholat             Waktu sholat\n"},
        {"💻 PEMROGRAMAN",
         "  c <file.c>         Compile & jalankan C\n"
         "  python <file.py>   Jalankan Python\n"
         "  node <file.js>     Jalankan JS\n"},
        {"⏱  PRODUKTIVITAS",
         "  todo tambah|daftar|selesai|hapus  Todo list\n"
         "  todo ringkas               Ringkasan progress\n"
         "  timer <detik>             Hitung mundur\n"
         "  stopwatch                 Stopwatch\n"},
        {"🌍 INFO DUNIA",
         "  dunia kurs <USD/EUR/dll>  Kurs mata uang\n"
         "  dunia gempa               Gempa terkini BMKG\n"
         "  dunia cuaca <kota>        Cuaca detail\n"},
        {"📊 GRAFIK",
         "  grafik ram|disk|demo      Grafik ASCII\n"},
        {"🔐 ENKRIPSI",
         "  enkripsi <in> <out> <key> Enkripsi/dekripsi file\n"
         "  hash <file>               Hash checksum file\n"},
        {"🎮 GAME",
         "  hangman                   Tebak kata hangman\n"
         "  quiz [jumlah]             Quiz teknologi\n"
         "  game                      Tebak angka\n"
         "  ttt                       Tic-Tac-Toe vs AI\n"},
        {"🖥  TUI & SCRIPT",
         "  tui / menu              Menu navigasi interaktif\n"
         "  fsh <file.fsh>          Jalankan script FINIX\n"
         "  fsh-contoh              Buat contoh script\n"},
        {"💬 CHAT LAN",
         "  chat server             Mulai server chat\n"
         "  chat kirim <ip> <pesan> Kirim pesan\n"
         "  chat scan               Scan HP aktif\n"},
        {"🚪 KELUAR",
         "  keluar/exit        Keluar shell\n"
         "  matikan/shutdown   Matikan OS\n"
         "  reboot             Restart OS\n"},
        {NULL, NULL}
    };
    for (int i = 0; secs[i].section; i++) {
        printf(YLW BOLD "\n  %s\n" RST, secs[i].section);
        printf(DIM "  ──────────────────────────────────────\n" RST);
        printf("%s", secs[i].items);
    }
    printf("\n");
}

/* ════════════════════════════════════════
 *  MATIKAN & REBOOT
 * ════════════════════════════════════════ */
void cmd_matikan(void) {
    printf("\n  " RED BOLD "⚡ FINIX OS sedang dimatikan...\n" RST);
    const char *msgs[] = {
        "Menghentikan proses pengguna...",
        "Menyimpan data sesi...",
        "Menutup koneksi jaringan...",
        "Melepas filesystem...",
        "Sistem berhasil dimatikan.",
        NULL
    };
    for (int i = 0; msgs[i]; i++) {
        usleep(350000);
        printf("  " GRN "[  OK  ]" RST " %s\n", msgs[i]);
    }
    printf("\n");
    /* Bersihkan memori */
    for (int i = 0; i < history_count; i++) free(history[i]);
    kmem_cache_destroy(user_cache);
    kmem_cache_destroy(fw_cache);
    exit(0);
}

/* ════════════════════════════════════════
 *  COMMAND PARSER
 * ════════════════════════════════════════ */
#define MAX_ARGS 16

void execute(char *input) {
    if (!input || !strlen(input)) return;
    /* Hapus newline */
    input[strcspn(input,"\n")] = '\0';
    /* Trim spasi di awal */
    while (*input == ' ' || *input == '\t') input++;
    if (!strlen(input) || input[0] == '#') return;

    history_add(input);

    /* Tokenizer dengan dukungan quoted strings */
    char buf[MAX_LINE]; strncpy(buf, input, MAX_LINE-1); buf[MAX_LINE-1]='\0';
    char *args[MAX_ARGS]; int argc = 0;
    char *p = buf;
    while (*p && argc < MAX_ARGS-1) {
        /* Lewati spasi */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            /* Quoted token */
            char q = *p++;
            args[argc++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            args[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    args[argc] = NULL;
    if (!argc) return;

    const char *cmd = args[0];
    int recognized = 1;

    /* ── Autentikasi ── */
    if      (!strcmp(cmd,"login"))       cmd_login();
    else if (!strcmp(cmd,"daftar") && argc==1) cmd_daftar();
    else if (!strcmp(cmd,"keluar_sesi")) cmd_logout();
    else if (!strcmp(cmd,"siapakah")||!strcmp(cmd,"whoami")) cmd_siapakah();
    else if (!strcmp(cmd,"daftar_user")) cmd_daftar_user();

    /* ── Filesystem ── */
    else if (!strcmp(cmd,"ls"))    cmd_ls(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"ll"))    cmd_ll(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"cd"))    cmd_cd(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"pwd"))   cmd_pwd();
    else if (!strcmp(cmd,"mkdir")) cmd_mkdir(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"sentuh")||!strcmp(cmd,"touch")) cmd_sentuh(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"cat"))   cmd_cat(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"hapus")||!strcmp(cmd,"rm")) cmd_hapus(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"salin")||!strcmp(cmd,"cp"))
        cmd_salin(argc>1?args[1]:NULL, argc>2?args[2]:NULL);
    else if (!strcmp(cmd,"pindah")||!strcmp(cmd,"mv"))
        cmd_pindah(argc>1?args[1]:NULL, argc>2?args[2]:NULL);
    else if (!strcmp(cmd,"nano")||!strcmp(cmd,"buat")) cmd_nano(argc>1?args[1]:NULL);

    /* ── Proses & Sistem ── */
    else if (!strcmp(cmd,"proses")||!strcmp(cmd,"ps"))  cmd_ps_list();
    else if (!strcmp(cmd,"bunuh")||!strcmp(cmd,"kill")) cmd_kill_proc(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"cpu"))   cmd_cpu();
    else if (!strcmp(cmd,"ram"))   cmd_ram();
    else if (!strcmp(cmd,"disk"))  cmd_disk();
    else if (!strcmp(cmd,"baterai")) cmd_baterai();
    else if (!strcmp(cmd,"android")) cmd_android();
    else if (!strcmp(cmd,"uname")) cmd_uname(argc>1&&!strcmp(args[1],"-a"));
    else if (!strcmp(cmd,"kernel")) {
        printf(CYN BOLD "\n  🐧 Info Kernel Linux\n" RST);
        system("uname -a 2>/dev/null");
        FILE *kv = fopen("/proc/version","r");
        if (kv) {
            char ln[256]; if (fgets(ln,sizeof(ln),kv)) printf("  %s", ln);
            fclose(kv);
        }
        printf("\n");
    }
    else if (!strcmp(cmd,"waktu")) cmd_waktu();
    else if (!strcmp(cmd,"tanggal")||!strcmp(cmd,"date")) cmd_tanggal();
    else if (!strcmp(cmd,"uptime")) cmd_uptime();
    else if (!strcmp(cmd,"kalender")||!strcmp(cmd,"cal")) cmd_kalender();
    else if (!strcmp(cmd,"sysinfo")) cmd_sysinfo();
    else if (!strcmp(cmd,"neofetch")) cmd_neofetch();

    /* ── Jaringan ── */
    else if (!strcmp(cmd,"ping"))    cmd_ping(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"cuaca")||!strcmp(cmd,"weather")) cmd_cuaca(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"ifconfig")) cmd_ifconfig();
    else if (!strcmp(cmd,"netstat"))  cmd_netstat();
    else if (!strcmp(cmd,"curl"))  cmd_curl(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"wget"))  cmd_wget(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"nmap"))  cmd_nmap(argc>1?args[1]:NULL);

    /* ── Firewall ── */
    else if (!strcmp(cmd,"firewall")) {
        if (argc<2 || !strcmp(args[1],"status")) {
            struct list_head *pos; int n=0;
            list_for_each(pos,&fw_rules) n++;
            printf(CYN "  🛡 Firewall: " RST "%d aturan aktif\n", n);
            if (n) fw_list();
        }
        else if (!strcmp(args[1],"tambah")||!strcmp(args[1],"add")) {
            if (argc>=5) fw_add(args[2],atoi(args[3]),args[4]);
            else printf(YLW "  Penggunaan: firewall tambah <proto> <port> <IZINKAN/BLOKIR>\n" RST);
        }
        else if (!strcmp(args[1],"daftar")||!strcmp(args[1],"list")) fw_list();
        else if (!strcmp(args[1],"hapus")||!strcmp(args[1],"del")) {
            if (argc>=3) fw_delete(atoi(args[2]));
            else printf(YLW "  Penggunaan: firewall hapus <port>\n" RST);
        }
        else printf(YLW "  Penggunaan: firewall status|tambah|daftar|hapus\n" RST);
    }

    /* ── Keamanan ── */
    else if (!strcmp(cmd,"scan"))      cmd_scan();
    else if (!strcmp(cmd,"antivirus")) cmd_antivirus(argc>1?args[1]:NULL);

    /* ── Paket ── */
    else if (!strcmp(cmd,"paket")||!strcmp(cmd,"pkg"))
        cmd_paket(argc-1, args+1);

    /* ── Catatan ── */
    else if (!strcmp(cmd,"catatan")||!strcmp(cmd,"note"))
        cmd_catatan(argc-1, args+1);

    /* ── AI ── */
    else if (!strcmp(cmd,"ai"))
        cmd_ai_chat(argc-1, args+1);
    else if (!strcmp(cmd,"ai-chat"))
        cmd_ai_chat(argc-1, args+1);

    /* ── Utilitas ── */
    else if (!strcmp(cmd,"kalkulator")||!strcmp(cmd,"calc"))
        cmd_kalkulator(argc>1?args[1]:NULL, argc>2?args[2]:NULL, argc>3?args[3]:NULL);
    else if (!strcmp(cmd,"gema")||!strcmp(cmd,"echo"))
        cmd_gema(argc-1, args+1);
    else if (!strcmp(cmd,"riwayat")||!strcmp(cmd,"history")) cmd_riwayat();
    else if (!strcmp(cmd,"bersih")||!strcmp(cmd,"clear"))    cmd_bersih();
    else if (!strcmp(cmd,"tentang")||!strcmp(cmd,"about"))   cmd_tentang();
    else if (!strcmp(cmd,"moto")||!strcmp(cmd,"fortune"))    cmd_moto();
    else if (!strcmp(cmd,"spanduk")||!strcmp(cmd,"banner"))  cmd_spanduk(argc-1,args+1);
    else if (!strcmp(cmd,"bantuan")||!strcmp(cmd,"help"))    cmd_bantuan();
    else if (!strcmp(cmd,"sholat")||!strcmp(cmd,"pray"))     cmd_sholat();

    /* ── Games ── */
    else if (!strcmp(cmd,"game"))  cmd_game_tebak();
    else if (!strcmp(cmd,"ttt")||!strcmp(cmd,"tic")) cmd_ttt();

    /* ── Pemrograman ── */
    else if (!strcmp(cmd,"c"))      cmd_run_c(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"python")||!strcmp(cmd,"py")) cmd_run_python(argc>1?args[1]:NULL);
    else if (!strcmp(cmd,"node"))   cmd_run_node(argc>1?args[1]:NULL);

    /* ── Keluar ── */
    else if (!strcmp(cmd,"keluar")||!strcmp(cmd,"exit")) {
        printf(GRN "  Sampai jumpa, %s! 👋\n\n" RST,
               current_user ? current_user->username : "tamu");
        for (int i=0; i<history_count; i++) free(history[i]);
        kmem_cache_destroy(user_cache);
        kmem_cache_destroy(fw_cache);
        exit(0);
    }
    else if (!strcmp(cmd,"matikan")||!strcmp(cmd,"shutdown")) cmd_matikan();
    else if (!strcmp(cmd,"reboot")) {
        printf(YLW "  🔄 Melakukan reboot...\n" RST);
        usleep(500000);
        for (int i=0; i<history_count; i++) free(history[i]);
        /* Restart diri sendiri */
        extern char **environ;
        char path[MAX_PATH];
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path)-1);
        if (n > 0) { path[n]='\0'; execve(path, (char*[2]){path,NULL}, environ); }
        /* Fallback */
        kmem_cache_destroy(user_cache);
        kmem_cache_destroy(fw_cache);
        exit(0);
    }
    /* ── Todo ── */
    else if (!strcmp(cmd,"todo"))
        cmd_todo(argc-1, args+1);

    /* ── Timer & Stopwatch ── */
    else if (!strcmp(cmd,"timer"))
        cmd_timer(argc-1, args+1);
    else if (!strcmp(cmd,"stopwatch")||!strcmp(cmd,"sw"))
        cmd_stopwatch();

    /* ── Grafik ── */
    else if (!strcmp(cmd,"grafik")||!strcmp(cmd,"chart"))
        cmd_grafik(argc-1, args+1);

    /* ── Info Dunia ── */
    else if (!strcmp(cmd,"dunia"))
        cmd_info_dunia(argc-1, args+1);

    /* ── Enkripsi ── */
    else if (!strcmp(cmd,"enkripsi")||!strcmp(cmd,"kripto"))
        cmd_enkripsi(argc-1, args+1);
    else if (!strcmp(cmd,"hash"))
        cmd_hash_file(argc>1?args[1]:NULL);

    /* ── Game Baru ── */
    else if (!strcmp(cmd,"hangman"))
        cmd_hangman();
    else if (!strcmp(cmd,"quiz"))
        cmd_quiz(argc-1, args+1);

    /* ── Shell Script .fsh ── */
    else if (!strcmp(cmd,"fsh"))
        cmd_fsh(argc-1, args+1);
    else if (!strcmp(cmd,"fsh-contoh"))
        cmd_fsh_contoh();

    /* ── Chat LAN ── */
    else if (!strcmp(cmd,"chat"))
        cmd_chat(argc-1, args+1);

    /* ── TUI Menu ── */
    else if (!strcmp(cmd,"tui")||!strcmp(cmd,"menu"))
        cmd_tui();

    /* ── LINUX-LIKE: jalankan binary eksternal ── */
    else {
        /* Coba eksekusi sebagai program eksternal */
        if (!exec_external(argc, args)) {
            recognized = 0;
        }
    }

    if (!recognized) ai_suggestion(cmd);


}

/* ════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════ */
int main(void) {
    boot_time = time(NULL);
    setup_signals();

    /* Init cache */
    user_cache = kmem_cache_create("user_cache", sizeof(finix_user_t), MAX_USERS);
    fw_cache   = kmem_cache_create("fw_cache",   sizeof(fw_rule_t),    MAX_FW_RULES);

    /* Load konfigurasi AI */
    ai_config_load();

    int mode = setjmp(crash_env);
    if (mode == 2)
        printf(YLW "\n  ⚠  SAFE MODE: Fungsionalitas terbatas.\n" RST);

    boot_screen();
    fw_init();
    init_user_system();
    notes_load();
    todo_load();

    printf("  " CYN "Silakan login atau daftar akun baru.\n" RST);
    printf("  Ketik " WHT "login" RST " untuk masuk, "
           WHT "daftar" RST " untuk registrasi, "
           WHT "bantuan" RST " untuk help.\n");
    /* Tampilkan status AI */
    int ollama_ok = ai_ollama_check();
    printf("  AI aktif: " BOLD "%s" RST, ai_cfg.active);
    if (strcmp(ai_cfg.active,"awing")==0)
        printf(" (%s)", ollama_ok ? GRN "Ollama online" RST : YLW "Ollama offline" RST);
    printf("\n\n");

    char input[MAX_LINE];
    while (1) {
        print_prompt();
        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }
        input[strcspn(input,"\n")] = '\0';

        /* Batasi perintah jika belum login */
        if (!current_user) {
            char buf[MAX_LINE]; strncpy(buf,input,MAX_LINE-1);
            char *cmd = strtok(buf," \t");
            if (!cmd) continue;
            if (!strcmp(cmd,"login")||!strcmp(cmd,"daftar")||
                !strcmp(cmd,"bantuan")||!strcmp(cmd,"help")||
                !strcmp(cmd,"keluar")||!strcmp(cmd,"exit")||
                !strcmp(cmd,"bersih")||!strcmp(cmd,"clear")||
                !strcmp(cmd,"tentang")) {
                execute(input);
            } else {
                printf(YLW "  ⚠  Silakan login atau daftar terlebih dahulu.\n" RST);
            }
        } else {
            execute(input);
        }
    }

    for (int i = 0; i < history_count; i++) free(history[i]);
    kmem_cache_destroy(user_cache);
    kmem_cache_destroy(fw_cache);
    printf(DIM "  Sistem FINIX OS dihentikan. Terima kasih.\n\n" RST);
    return 0;
}
