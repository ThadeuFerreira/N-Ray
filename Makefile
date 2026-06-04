# Wrapper around premake5 + the vendored raylib build.
#
#   make            # build Release (default)
#   make CONFIG=debug_x64
#   make build-performance # build Performance with aggressive CPU flags
#   make run-performance   # build + run the Performance binary
#   make raylib     # (re)build vendor/raylib/src/libraylib.a only
#   make generate   # regenerate build/ project files only
#   make run        # build + run from PathTracingRenderer/ (needs models/ + textures/)
#   make clean      # remove build/, bin/, obj/ (keeps the raylib lib)
#   make distclean  # also clean the vendored raylib objects/lib
#
# premake5.lua sets `location "build"`, so generated makefiles live under build/.

.PHONY: all generate build build-performance raylib run run-performance clean distclean

CONFIG     ?= release_x64
PERF_CONFIG := performance_x64
RAYLIB_DIR := vendor/raylib/src
RAYLIB_LIB := $(RAYLIB_DIR)/libraylib.a

all: build

# Build the static raylib library from the vendored source. raylib disables HDR
# loading by default (SUPPORT_FILEFORMAT_HDR 0); the renderer hard-depends on
# textures/HDRI.hdr, so enable it here before building. vendor/raylib is a git
# submodule, so doing this in the recipe survives a fresh `git submodule update`
# (which would otherwise reset config.h and silently drop HDR support).
$(RAYLIB_LIB):
	@grep -q 'SUPPORT_FILEFORMAT_HDR      1' $(RAYLIB_DIR)/config.h || \
		sed -i 's/\(#[[:space:]]*define[[:space:]]\+SUPPORT_FILEFORMAT_HDR[[:space:]]\+\)0/\11/' $(RAYLIB_DIR)/config.h
	$(MAKE) -C $(RAYLIB_DIR) PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC

raylib: $(RAYLIB_LIB)

generate:
	premake5 gmake2

build: raylib generate
	$(MAKE) -C build config=$(CONFIG)

build-performance: raylib generate
	$(MAKE) -C build config=$(PERF_CONFIG)

run: build
	cd PathTracingRenderer && ../bin/$(if $(findstring debug,$(CONFIG)),Debug,Release)/PathTracingRenderer

run-performance: build-performance
	cd PathTracingRenderer && ../bin/Performance/PathTracingRenderer

clean:
	rm -rf build bin obj

distclean: clean
	$(MAKE) -C $(RAYLIB_DIR) clean || true
	rm -f $(RAYLIB_LIB)
