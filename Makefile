CC ?= cc
AR ?= ar
CPPFLAGS ?= -Iinclude -Ithird_party
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic
LDLIBS += -lm

CORE_SRC = src/core/image.c src/core/image_io.c src/core/lut3d.c src/core/presets.c src/core/pipeline.c src/core/stb_impl.c
CORE_OBJ = $(CORE_SRC:.c=.o)

.PHONY: all clean check install fetch-luts fetch-polaroid-lut

all: polajuice libpolajuice.a

# third-party code compiles in its own TU with warnings off; ours stay strict
src/core/stb_impl.o: CFLAGS += -w

libpolajuice.a: $(CORE_OBJ)
	$(AR) rcs $@ $^

polajuice: src/cli/main.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ src/cli/main.o libpolajuice.a $(LDLIBS)

tests/test_core: tests/test_core.o libpolajuice.a
	$(CC) $(CFLAGS) -o $@ tests/test_core.o libpolajuice.a $(LDLIBS)

check: tests/test_core
	./tests/test_core

fetch-luts:
	sh scripts/fetch_luts.sh

fetch-polaroid-lut:
	curl -fsSL https://gmic.eu/color_presets/instant_consumer/clut/polaroid_px-680.zip -o polaroid_px-680.zip
	unzip -o polaroid_px-680.zip polaroid_px-680.cube
	$(RM) polaroid_px-680.zip

install: all
	install -Dm755 polajuice $(DESTDIR)/usr/local/bin/polajuice
	install -Dm644 include/polajuice.h $(DESTDIR)/usr/local/include/polajuice.h
	install -Dm644 libpolajuice.a $(DESTDIR)/usr/local/lib/libpolajuice.a

clean:
	$(RM) $(CORE_OBJ) src/cli/main.o tests/test_core.o
	$(RM) polajuice libpolajuice.a tests/test_core
