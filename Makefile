CC ?= cc
AR ?= ar
CPPFLAGS ?= -Iinclude -Ithird_party
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic
# Auto header dependencies (picked up by the built-in .c -> .o rule).
# Without them, editing internal.h leaves stale objects compiled against a
# different PjPreset layout, which manifests as garbage at runtime.
CFLAGS += -MMD -MP
LDLIBS += -lm

CORE_SRC = src/core/image.c src/core/image_io.c src/core/lut3d.c src/core/presets.c src/core/pipeline.c src/core/stb_impl.c
CORE_OBJ = $(CORE_SRC:.c=.o)
ALL_OBJ = $(CORE_OBJ) src/cli/main.o tests/test_core.o

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

# portable install: BSD install (macOS) has no -D, so create dirs explicitly
install: all
	mkdir -p $(DESTDIR)/usr/local/bin $(DESTDIR)/usr/local/include $(DESTDIR)/usr/local/lib
	install -m 755 polajuice $(DESTDIR)/usr/local/bin/polajuice
	install -m 644 include/polajuice.h $(DESTDIR)/usr/local/include/polajuice.h
	install -m 644 libpolajuice.a $(DESTDIR)/usr/local/lib/libpolajuice.a

clean:
	$(RM) $(ALL_OBJ) $(ALL_OBJ:.o=.d)
	$(RM) polajuice libpolajuice.a tests/test_core

-include $(ALL_OBJ:.o=.d)
