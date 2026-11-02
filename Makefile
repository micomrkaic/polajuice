CC ?= cc
AR ?= ar
# Install layout (GNU-ish, per-user by default): binaries in $(bindir),
# the film library, print stocks and web page under $(datadir), man pages
# in $(mandir). 'make install PREFIX=/usr/local' for a system-wide one.
PREFIX ?= $(HOME)/.local
bindir ?= $(PREFIX)/bin
datadir ?= $(PREFIX)/share/polajuice
mandir ?= $(PREFIX)/share/man/man1
includedir ?= $(PREFIX)/include
libdir ?= $(PREFIX)/lib
CPPFLAGS ?= -Iinclude -Isrc/cli -Isrc/core -Ithird_party
# the binaries know the install's datadir as the last place to look for
# assets (after $POLAJUICE_*, the source tree and ~/.local/share/polajuice)
CPPFLAGS += -DPOLAJUICE_DATADIR=\"$(datadir)\"
CFLAGS ?= -O3 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -pthread
# Auto header dependencies (picked up by the built-in .c -> .o rule).
# Without them, editing internal.h leaves stale objects compiled against a
# different PjPreset layout, which manifests as garbage at runtime.
CFLAGS += -MMD -MP
LDLIBS += -lm -pthread
# macOS hides XSI extensions (_SC_NPROCESSORS_ONLN, strdup's friends) once a
# file asks for strict POSIX; ask for the Darwin superset everywhere
ifeq ($(shell uname -s),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
endif

CORE_SRC = src/core/image.c src/core/image_io.c src/core/lut3d.c src/core/presets.c src/core/pipeline.c src/core/parallel.c src/core/jpegenc.c src/core/recipe.c src/core/rawdec.c src/core/stb_impl.c
CORE_OBJ = $(CORE_SRC:.c=.o)
CLI_OBJ = src/cli/filmlib.o src/cli/recipe.o src/cli/look.o src/cli/serve.o
ALL_OBJ = $(CORE_OBJ) src/cli/main.o $(CLI_OBJ) src/movie/superjuice.o tests/test_core.o

.PHONY: all clean clean-wasm check check-wasm install install-files install-luts install-dev uninstall fetch-luts fetch-polaroid-lut wasm serve

all: polajuice superjuice libpolajuice.a

# third-party code compiles in its own TU with warnings off; ours stay strict
src/core/stb_impl.o: CFLAGS += -w

libpolajuice.a: $(CORE_OBJ)
	$(AR) rcs $@ $^

polajuice: src/cli/main.o $(CLI_OBJ) libpolajuice.a
	$(CC) $(CFLAGS) -o $@ src/cli/main.o $(CLI_OBJ) libpolajuice.a $(LDLIBS)

tests/test_core: tests/test_core.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ tests/test_core.o libpolajuice.a $(LDLIBS)

superjuice: src/movie/superjuice.o src/cli/filmlib.o src/cli/recipe.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ src/movie/superjuice.o src/cli/filmlib.o src/cli/recipe.o libpolajuice.a $(LDLIBS)

check: tests/test_core
	./tests/test_core
	sh scripts/test_compat.sh
	sh scripts/test_movie.sh
	sh scripts/test_threads.sh
	sh scripts/test_serve.sh
	sh scripts/test_install.sh

# WebAssembly build via wasi-sdk (auto-downloaded to third_party/ on
# first run; see docs/WEB.md). 'make' builds native; 'make wasm' the
# browser engine; 'make check-wasm' verifies it under node.
wasm:
	sh scripts/build_web.sh

check-wasm:
	cd scripts && node test_wasm.mjs && node test_page.mjs && node test_page_dom.mjs && node test_workspace.mjs

# 'clean' deliberately keeps web/polajuice.wasm: release tarballs ship it
# prebuilt and release.sh runs 'make clean' before verifying it.
clean-wasm:
	$(RM) web/polajuice.wasm

# the workspace on the native engine; DIR= picks the folder to work in
serve: polajuice
	./polajuice serve $(DIR)

fetch-luts:
	sh scripts/fetch_luts.sh

fetch-polaroid-lut:
	curl -fsSL https://gmic.eu/color_presets/instant_consumer/clut/polaroid_px-680.zip -o polaroid_px-680.zip
	unzip -o polaroid_px-680.zip polaroid_px-680.cube
	$(RM) polaroid_px-680.zip

# ---- install ----------------------------------------------------------
# 'make install' puts polajuice, superjuice and super8ify on the PATH,
# the print stocks and the web page under $(datadir), the man pages
# under $(mandir), and fills $(datadir)/luts with the film library: a
# library already fetched into the source tree is copied, whatever is
# still missing is downloaded (scripts/fetch_luts.sh skips families it
# has). 'make install-files' is the same without the network; 'make
# uninstall' removes everything including the fetched library.
# Portable: BSD install (macOS) has no -D, so directories are made first.
install: install-files install-luts
	@echo "installed to $(bindir) (data in $(datadir), man pages in $(mandir))"
	@case ":$$PATH:" in *":$(bindir):"*) ;; *) echo "note: $(bindir) is not on your PATH" ;; esac

install-files: all
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(datadir)/prints $(DESTDIR)$(datadir)/web $(DESTDIR)$(mandir)
	install -m 755 polajuice $(DESTDIR)$(bindir)/polajuice
	install -m 755 superjuice $(DESTDIR)$(bindir)/superjuice
	install -m 755 scripts/super8ify.sh $(DESTDIR)$(bindir)/super8ify
	for f in data/prints/*.cube data/prints/README.md; do install -m 644 $$f $(DESTDIR)$(datadir)/prints/; done
	install -m 644 web/index.html web/engine.js $(DESTDIR)$(datadir)/web/
	[ ! -f web/polajuice.wasm ] || install -m 644 web/polajuice.wasm $(DESTDIR)$(datadir)/web/
	cp -R web/samples web/vendor $(DESTDIR)$(datadir)/web/
	for m in polajuice superjuice super8ify; do \
	    sed "s/@VERSION@/$$(cat VERSION)/" docs/man/$$m.1 > $(DESTDIR)$(mandir)/$$m.1 && chmod 644 $(DESTDIR)$(mandir)/$$m.1; done

# the film library: copy what the source tree has, fetch the rest
install-luts:
	mkdir -p $(DESTDIR)$(datadir)/luts
	@if [ -d data/luts ]; then cp -R data/luts/. $(DESTDIR)$(datadir)/luts/; fi
	LUT_DIR=$(DESTDIR)$(datadir)/luts sh scripts/fetch_luts.sh

# the C library and header, for programs that link libpolajuice
install-dev: libpolajuice.a
	mkdir -p $(DESTDIR)$(includedir) $(DESTDIR)$(libdir)
	install -m 644 include/polajuice.h $(DESTDIR)$(includedir)/polajuice.h
	install -m 644 libpolajuice.a $(DESTDIR)$(libdir)/libpolajuice.a

uninstall:
	$(RM) $(DESTDIR)$(bindir)/polajuice $(DESTDIR)$(bindir)/superjuice $(DESTDIR)$(bindir)/super8ify
	$(RM) $(DESTDIR)$(mandir)/polajuice.1 $(DESTDIR)$(mandir)/superjuice.1 $(DESTDIR)$(mandir)/super8ify.1
	$(RM) $(DESTDIR)$(includedir)/polajuice.h $(DESTDIR)$(libdir)/libpolajuice.a
	$(RM) -r $(DESTDIR)$(datadir)

clean:
	$(RM) $(ALL_OBJ) $(ALL_OBJ:.o=.d)
	$(RM) polajuice superjuice libpolajuice.a tests/test_core

-include $(ALL_OBJ:.o=.d)
