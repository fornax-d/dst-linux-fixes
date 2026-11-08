/* Exit without the crash. The game tears down OpenGL in an order some drivers
 * crash on (SIGSEGV in libX11 via libGLX_nvidia, glvnd asserts, MangoHud).
 * Settings are saved before the first SDL_Quit (window destructor), so from
 * then on: SDL_Quit does nothing, a fatal signal ends the process quietly,
 * and main()'s return goes straight to _exit(). DST_FAST_EXIT=0 disables it. */
#include "common.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>

static int enabled(void) {
    return is_game() && env_int("DST_FAST_EXIT", 1) != 0;
}

static void quiet_exit(int sig) {
    (void)sig;
    _exit(0);
}

void SDL_Quit(void) {
    if (enabled()) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = quiet_exit;
        for (int s = 0, sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL }; s < 4; s++)
            sigaction(sigs[s], &sa, NULL);
        return;
    }
    static void (*real)(void);
    if (!real) real = (void (*)(void))sdl_sym("SDL_Quit");
    real();
}

typedef int (*main_fn)(int, char **, char **);
typedef int (*start_fn)(main_fn, int, char **, void (*)(void), void (*)(void), void (*)(void), void *);
static main_fn game_main;

static int main_then_exit(int argc, char **argv, char **envp) {
    int ret = game_main(argc, argv, envp);
    fflush(NULL);
    _exit(ret);
}

/* The game's crt1 calls the GLIBC_2.2.5 version and passes init; forward to the same. */
int __libc_start_main(main_fn main, int argc, char **argv, void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end) {
    void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
    start_fn start = libc ? (start_fn)dlvsym(libc, "__libc_start_main", "GLIBC_2.2.5") : NULL;
    if (!start) start = (start_fn)dlsym(RTLD_NEXT, "__libc_start_main");
    if (enabled()) {
        game_main = main;
        main = main_then_exit;
    }
    return start(main, argc, argv, init, fini, rtld_fini, stack_end);
}
