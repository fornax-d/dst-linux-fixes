/* Options from the in-game "Linux" tab (the "Linux options" mod).
 *
 * The mod saves  <klei>/<user>/client_save/dst_linux_options  (KEY=VALUE lines).
 * At start they become environment variables, before the other constructors
 * read them; variables already set (Steam launch options) win.
 *
 * The mod reads  .../client_save/dst_linux_status  to show what this run uses.
 * On disk that file only says "loaded=0"; while we are loaded, opening it
 * returns the real status from memory. So without us the tab sees "not loaded"
 * instead of a stale status from an earlier run. */
#include "common.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define OPTIONS_FILE "dst_linux_options"
#define STATUS_FILE "dst_linux_status"
#define HEADER "KLEI     1 "   /* persistent string, not encoded */

/* stat() is GLIBC_2.33; the old entry point works everywhere. */
int __xstat(int ver, const char *path, struct stat *st);
__asm__(".symver __xstat,__xstat@GLIBC_2.2.5");
#define stat(path, st) __xstat(1, path, st)

static const char *const keys[] = {
    "DST_WAYLAND", "DST_FPS_LIMIT", "DST_SWAP_INTERVAL", "DST_FAST_EXIT", "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS",
};
#define NKEYS (int)(sizeof keys / sizeof *keys)

static char status[1024];
static int status_len;

static int known_key(const char *k, size_t n) {
    for (int i = 0; i < NKEYS; i++)
        if (strlen(keys[i]) == n && !memcmp(keys[i], k, n)) return 1;
    return 0;
}

/* Calls fn(dir) for every <klei>/<user>/client_save directory. */
static void each_client_save(void (*fn)(const char *dir, void *arg), void *arg) {
    const char *home = getenv("HOME");
    char root[512], dir[1024];
    if (!home || snprintf(root, sizeof root, "%s/.klei/DoNotStarveTogether", home) >= (int)sizeof root) return;
    DIR *d = opendir(root);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        struct stat st;
        if (e->d_name[0] == '.' || snprintf(dir, sizeof dir, "%s/%s/client_save", root, e->d_name) >= (int)sizeof dir)
            continue;
        if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) fn(dir, arg);
    }
    closedir(d);
}

struct newest { char path[1100]; time_t mtime; };

static void find_options(const char *dir, void *arg) {
    struct newest *n = arg;
    char path[1100];
    struct stat st;
    snprintf(path, sizeof path, "%s/" OPTIONS_FILE, dir);
    if (stat(path, &st) == 0 && (!n->path[0] || st.st_mtime > n->mtime)) {
        strcpy(n->path, path);
        n->mtime = st.st_mtime;
    }
}

/* KEY=VALUE lines; unknown keys and odd values are ignored. */
static void apply_options(const char *path) {
    char buf[4096];
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t len = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[len] = 0;
    char *p = buf, *line;
    if (!strncmp(p, "KLEI", 4)) p += strlen(HEADER);
    while ((line = strsep(&p, "\n"))) {
        char *eq = strchr(line, '='), *v = eq + 1;
        if (!eq || !known_key(line, (size_t)(eq - line))) continue;
        size_t vn = strspn(v, "abcdefghijklmnopqrstuvwxyz0123456789");
        if (!vn || (v[vn] != 0 && v[vn] != '\r')) continue;
        v[vn] = *eq = 0;
        setenv(line, v, 0);
    }
}

static void write_placeholder(const char *dir, void *arg) {
    (void)arg;
    static const char text[] = HEADER "loaded=0\n";
    char path[1100], old[64];
    snprintf(path, sizeof path, "%s/" STATUS_FILE, dir);
    FILE *f = fopen(path, "r");
    size_t n = f ? fread(old, 1, sizeof old, f) : 0;
    if (f) fclose(f);
    if (n == sizeof text - 1 && !memcmp(old, text, n)) return;
    if ((f = fopen(path, "w"))) {
        fputs(text, f);
        fclose(f);
    }
}

static void add(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(status + status_len, sizeof status - status_len, fmt, ap);
    va_end(ap);
    if (n > 0 && status_len + n < (int)sizeof status) status_len += n;
}

/* Runs before the default-priority constructors in sdl.c, fps.c. */
__attribute__((constructor(101)))
static void load_config(void) {
    if (!is_game()) return;
    char from_env[NKEYS];
    for (int i = 0; i < NKEYS; i++) from_env[i] = getenv(keys[i]) != NULL;

    struct newest n = { "", 0 };
    each_client_save(find_options, &n);
    if (n.path[0]) apply_options(n.path);
    each_client_save(write_placeholder, NULL);

    /* same rule as set_sdl_env() in sdl.c */
    const char *drv = getenv("SDL_VIDEODRIVER");
    int wayland = drv ? !strcmp(drv, "wayland") : env_int("DST_WAYLAND", getenv("WAYLAND_DISPLAY") != NULL) != 0;

    add(HEADER "loaded=1\ndisplay=%s\n", wayland ? "wayland" : "x11");
    for (int i = 0; i < NKEYS; i++) {
        const char *v = getenv(keys[i]);
        add("%s=%s\n", keys[i], v ? v : "");
    }
    add("env=");
    for (int i = 0, first = 1; i < NKEYS; i++)
        if (from_env[i]) add(first ? "%s" : ",%s", keys[i]), first = 0;
    add("\n");
}

static int is_status(const char *path) {
    static const char suffix[] = "client_save/" STATUS_FILE;
    size_t n = path ? strlen(path) : 0;
    return status_len && n >= sizeof suffix - 1 && !strcmp(path + n - (sizeof suffix - 1), suffix);
}

/* A fresh in-memory file with the status, positioned at its start. */
static int status_fd(void) {
    int fd = (int)syscall(SYS_memfd_create, "dst_linux_status", 0);
    if (fd < 0) return -1;
    if (write(fd, status, status_len) != status_len) { close(fd); return -1; }
    lseek(fd, 0, SEEK_SET);
    return fd;
}

static void *libc_sym(const char *name) {
    void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
    return libc ? dlsym(libc, name) : NULL;
}

/* The game reads persistent strings through these (only plain reads are redirected). */
FILE *fopen(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (is_status(path) && mode[0] == 'r' && !strchr(mode, '+')) {
        int fd = status_fd();
        FILE *f = fd >= 0 ? fdopen(fd, "r") : NULL;
        if (f) return f;
        if (fd >= 0) close(fd);
    }
    if (!real) real = (FILE *(*)(const char *, const char *))libc_sym("fopen");
    return real(path, mode);
}

int open(const char *path, int flags, ...) {
    static int (*real)(const char *, int, ...);
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (is_status(path) && (flags & O_ACCMODE) == O_RDONLY) {
        int fd = status_fd();
        if (fd >= 0) return fd;
    }
    if (!real) real = (int (*)(const char *, int, ...))libc_sym("open");
    return real(path, flags, mode);
}
