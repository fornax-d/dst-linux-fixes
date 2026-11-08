#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "[dst_fixes] "
/* sdl2-compat inside the Steam Linux Runtime container */
#define RUNTIME_SDL "/usr/lib/x86_64-linux-gnu/sdl2-compat/libSDL2-2.0.so.0"

/* Oldest symbol versions, so the library loads on any x86-64 glibc. */
__asm__(".symver dlopen,dlopen@GLIBC_2.2.5");
__asm__(".symver dlsym,dlsym@GLIBC_2.2.5");
__asm__(".symver dlvsym,dlvsym@GLIBC_2.2.5");
__asm__(".symver dladdr,dladdr@GLIBC_2.2.5");
__asm__(".symver dlerror,dlerror@GLIBC_2.2.5");

#define INTERNAL __attribute__((visibility("hidden")))

INTERNAL int is_game(void);                 /* the client, not a dedicated server or helper */
INTERNAL int env_int(const char *name, int def);
INTERNAL void *sdl_sym(const char *name);   /* the runtime SDL's function; aborts if missing */
