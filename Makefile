# make           build/dst_fixes.so
# make prebuilt  also copy it to prebuilt/
# make check     show the glibc versions and libraries it needs

CC     ?= cc
CFLAGS ?= -O2
RUNTIME_SDL := /usr/lib/x86_64-linux-gnu/sdl2-compat/libSDL2-2.0.so.0
SRC := src/common.c src/sdl.c src/fps.c src/exit.c

all: build/dst_fixes.so

# The soname makes it stand in for the game's libSDL2; linking against an empty
# library whose soname is the runtime path records that path as a dependency.
build/dst_fixes.so: $(SRC) src/common.h build/runtime_sdl.so
	$(CC) $(CFLAGS) -Wall -Wextra -shared -fPIC -Wl,-soname,libSDL2-2.0.so.0 -o $@ $(SRC) \
		-Wl,--no-as-needed build/runtime_sdl.so -l:libdl.so.2

build/runtime_sdl.so:
	mkdir -p build
	printf '' | $(CC) -shared -nostdlib -Wl,-soname,$(RUNTIME_SDL) -o $@ -x c -

prebuilt: all
	cp build/dst_fixes.so prebuilt/

check: all
	@printf 'glibc: '; objdump -T build/dst_fixes.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tr '\n' ' '; echo
	@readelf -dW build/dst_fixes.so | grep -E 'NEEDED|SONAME'

clean:
	rm -rf build

.PHONY: all prebuilt check clean
