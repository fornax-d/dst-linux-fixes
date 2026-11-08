/* Frame limit. DST_FPS_LIMIT: unset/0 = none, N = N fps, game = keep the game's 60. */
#include "common.h"
#include <errno.h>
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAX_SITES 8

/* The main loop reloads its frame interval (1/60 s) every frame:
 *   movss xmm0,[rbp+disp32] ; mov rdi,reg ; call Timer::SetInterval
 * Each such load is pointed at our own float instead. */
static int is_interval_load(const unsigned char *p) {
    return p[0] == 0xf3 && p[1] == 0x0f && p[2] == 0x10 && p[3] == 0x85 && p[6] == 0 && p[7] == 0 &&
           (p[8] == 0x48 || p[8] == 0x4c) && p[9] == 0x89 && p[11] == 0xe8;
}

static const unsigned char *rel32_target(const unsigned char *rel, const unsigned char *next) {
    int32_t d;
    memcpy(&d, rel, 4);
    return next + d;
}

static int make_writable(void *addr, size_t len, int writable) {
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t lo = (uintptr_t)addr & ~(page - 1);
    uintptr_t hi = ((uintptr_t)addr + len + page - 1) & ~(page - 1);
    return mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC | (writable ? PROT_WRITE : 0));
}

/* A float within rel32 reach: the unused tail of the last .bss page. */
static float *alloc_slot(uintptr_t bss_end) {
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t slot = (bss_end + 15) & ~(uintptr_t)15, page_end = (bss_end + page - 1) & ~(page - 1);
    if (slot + sizeof(float) <= page_end) return (float *)slot;
    void *p = mmap((void *)page_end, page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (p != (void *)page_end) { munmap(p, page); return NULL; }
    return p;
}

/* The game's wait (0x3e20f0) sleeps the whole remainder and restarts the timer
 * after waking, so oversleep adds up (144 -> ~142 fps), and it busy-waits the
 * last 2 ms. This one sleeps to absolute deadlines instead. */
static int64_t period_ns, deadline_ns;

static void pace_frame(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = ts.tv_sec * 1000000000LL + ts.tv_nsec, next = deadline_ns + period_ns;
    if (next <= now) { deadline_ns = now; return; }   /* late: restart the grid, don't catch up */
    deadline_ns = next;
    ts = (struct timespec){ next / 1000000000, next % 1000000000 };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

/* The second interval load is preceded by the main loop's call of the wait; it
 * has no side effects and its result is unused. The call goes to a
 * `jmp [rip]; .quad pace_frame` trampoline in the zero padding after the code. */
static int redirect_wait(unsigned char **sites, int n, uintptr_t text, uintptr_t text_end) {
    unsigned char *call = NULL;
    for (int i = 0; i < n; i++) {
        unsigned char *c = sites[i] - 5;
        if ((uintptr_t)c < text || c[0] != 0xe8) continue;
        const unsigned char *t = rel32_target(c + 1, c + 5);
        if (call || (uintptr_t)t < text || (uintptr_t)t + 4 > text_end || memcmp(t, "\xf3\x0f\x1e\xfa", 4))
            return 0;   /* not exactly one call, or not to a function entry (endbr64) */
        call = c;
    }
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t tramp = (text_end + 15) & ~(uintptr_t)15, page_end = (text_end + page - 1) & ~(page - 1);
    if (!call || tramp + 14 > page_end) return 0;
    for (uintptr_t p = text_end; p < page_end; p++)
        if (*(unsigned char *)p) return 0;
    intptr_t rel = (intptr_t)tramp - (intptr_t)(call + 5);
    if (rel != (int32_t)rel) return 0;

    void (*fn)(void) = pace_frame;
    int32_t rel32 = (int32_t)rel;
    if (make_writable((void *)tramp, 14, 1) || make_writable(call, 5, 1)) return 0;
    memcpy((void *)tramp, "\xff\x25\0\0\0\0", 6);
    memcpy((void *)(tramp + 6), &fn, 8);
    memcpy(call + 1, &rel32, 4);
    make_writable(call, 5, 0);
    make_writable((void *)tramp, 14, 0);
    return 1;
}

static int patch(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    uintptr_t base = info->dlpi_addr, text = 0, text_end = 0, bss_end = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_flags & PF_W) bss_end = base + ph->p_vaddr + ph->p_memsz;
        if ((ph->p_flags & PF_X) && !text) text = base + ph->p_vaddr, text_end = text + ph->p_memsz;
    }

    unsigned char *sites[MAX_SITES];
    int n = 0;
    for (unsigned char *p = (unsigned char *)text; text && p + 16 <= (unsigned char *)text_end; p++) {
        if (!is_interval_load(p)) continue;
        if (n == MAX_SITES || (n && rel32_target(p + 12, p + 16) != rel32_target(sites[0] + 12, sites[0] + 16))) {
            fprintf(stderr, TAG "unknown game build, fps limit not changed\n");
            return 1;
        }
        sites[n++] = p;
    }
    float *slot = n && bss_end ? alloc_slot(bss_end) : NULL;
    if (!slot) {
        fprintf(stderr, TAG "unknown game build, fps limit not changed\n");
        return 1;
    }

    const char *env = getenv("DST_FPS_LIMIT");
    int limit = env && *env ? env_int("DST_FPS_LIMIT", -1) : 0;
    if (limit < 0) {
        fprintf(stderr, TAG "DST_FPS_LIMIT=%s is not a number >= 0 or \"game\", using 0\n", env);
        limit = 0;
    }
    *slot = limit ? 1.0f / limit : 0.0f;   /* 0: the game's wait returns at once */

    for (int i = 0; i < n; i++)
        if ((intptr_t)slot - (intptr_t)(sites[i] + 8) != (int32_t)((intptr_t)slot - (intptr_t)(sites[i] + 8)))
            return 1;
    for (int i = 0; i < n; i++) {
        int32_t disp = (int32_t)((intptr_t)slot - (intptr_t)(sites[i] + 8));
        if (make_writable(sites[i], 8, 1)) return 1;
        sites[i][3] = 0x05;   /* movss xmm0,[rip+disp32] */
        memcpy(sites[i] + 4, &disp, 4);
        make_writable(sites[i], 8, 0);
    }
    if (limit) {
        period_ns = 1000000000 / limit;
        int paced = redirect_wait(sites, n, text, text_end);
        fprintf(stderr, TAG "fps limit %d%s\n", limit, paced ? "" : " (game's own wait)");
    }
    return 1;   /* the first object is the executable; stop there */
}

__attribute__((constructor))
static void init_fps(void) {
    const char *env = getenv("DST_FPS_LIMIT");
    if (is_game() && !(env && strcmp(env, "game") == 0))
        dl_iterate_phdr(patch, NULL);
}
