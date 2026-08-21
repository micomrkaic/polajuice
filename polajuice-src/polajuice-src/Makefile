CC ?= cc
AR ?= ar
CPPFLAGS ?= -Iinclude -Isrc/cli -Ithird_party
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic
# Auto header dependencies (picked up by the built-in .c -> .o rule).
# Without them, editing internal.h leaves stale objects compiled against a
# different PjPreset layout, which manifests as garbage at runtime.
CFLAGS += -MMD -MP
LDLIBS += -lm

CORE_SRC = src/core/image.c src/core/image_io.c src/core/lut3d.c src/core/presets.c src/core/pipeline.c src/core/stb_impl.c
CORE_OBJ = $(CORE_SRC:.c=.o)
ALL_OBJ = $(CORE_OBJ) src/cli/main.o src/cli/filmlib.o src/movie/superjuice.o tests/test_core.o

.PHONY: all clean clean-wasm check check-wasm install fetch-luts fetch-polaroid-lut wasm serve

all: polajuice superjuice libpolajuice.a

# third-party code compiles in its own TU with warnings off; ours stay strict
src/core/stb_impl.o: CFLAGS += -w

libpolajuice.a: $(CORE_OBJ)
	$(AR) rcs $@ $^

polajuice: src/cli/main.o src/cli/filmlib.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ src/cli/main.o src/cli/filmlib.o libpolajuice.a $(LDLIBS)

tests/test_core: tests/test_core.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ tests/test_core.o libpolajuice.a $(LDLIBS)

superjuice: src/movie/superjuice.o src/cli/filmlib.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ src/movie/superjuice.o src/cli/filmlib.o libpolajuice.a $(LDLIBS)

check: tests/test_core
	./tests/test_core
	sh scripts/test_compat.sh
	sh scripts/test_movie.sh

# WebAssembly build via wasi-sdk (auto-downloaded to third_party/ on
# first run; see docs/WEB.md). 'make' builds native; 'make wasm' the
# browser engine; 'make check-wasm' verifies it under node.
wasm:
	sh scripts/build_web.sh

check-wasm:
	cd scripts && node test_wasm.mjs && node test_page.mjs

# 'clean' deliberately keeps web/polajuice.wasm: release tarballs ship it
# prebuilt and release.sh runs 'make clean' before verifying it.
clean-wasm:
	$(RM) web/polajuice.wasm

serve:
	@echo "serving web/ at http://localhost:8000 (Ctrl-C to stop)"
	cd web && python3 -m http.server 8000

fetch-luts:
	sh scripts/fetch_luts.sh

fetch-polaroid-lut:
	curl -fsSL https://gmic.eu/color_presets/instant_consumer/clut/polaroid_px-680.zip -o polaroid_px-680.zip
	unzip -o polaroid_px-680.zip polaroid_px-680.cube
	$(RM) polaroid_px-680.zip

# portable install: BSD install (macOS) has no -D, so create dirs explicitly
install: all
	mkdir -p $(DESTDIR)/usr/local/bin $(DESTDIR)/usr/local/include $(DESTDIR)/usr/local/lib
	install -m 755 polajuice $(DESTDIR)/usr/local/bin/polajuice
	install -m 644 include/polajuice.h $(DESTDIR)/usr/local/include/polajuice.h
	install -m 644 libpolajuice.a $(DESTDIR)/usr/local/lib/libpolajuice.a

clean:
	$(RM) $(ALL_OBJ) $(ALL_OBJ:.o=.d)
	$(RM) polajuice superjuice libpolajuice.a tests/test_core

-include $(ALL_OBJ:.o=.d)
