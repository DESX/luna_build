# luna_build: a static, drop-in ninja that parses build.luna (Lua) files.
# Assembled from upstream ninja + lua with graft (patch + overlay).

SHELL := /bin/bash

b  := build
DL := .cache

MAKEFLAGS := -j 8

all: $b/luna_build

# --- self-bootstrap graft, pinned to a release tag ---
.cache/graft/graft.mk:; @git clone -q --depth=1 -b v1.0.0 https://github.com/DESX/graft.git $(dir $@)
include .cache/graft/graft.mk

# --- lua: built into a static lib (no readline / dlopen) ---
LUA_DIR     := $b/lua
LUA_TGT     := $(LUA_DIR)/src/lua.h
LUA_TAR     := $(DL)/lua.tar.gz
LUA_TAR_URL := https://www.lua.org/ftp/lua-5.4.7.tar.gz
$(eval $(call FETCH,LUA))

LUA_A := $(LUA_DIR)/src/liblua.a
$(LUA_A): $(LUA_TGT)
	$(MAKE) -C $(LUA_DIR)/src a CC=cc "MYCFLAGS=-DLUA_USE_POSIX -O2"

# --- ninja: patched (src/ninja.cc) + overlaid (src/luna.{cc,h}) ---
NINJA_DIR     := $b/ninja
NINJA_TGT     := $(NINJA_DIR)/src/luna.cc      # present only after overlay
NINJA_TAR     := $(DL)/ninja.tar.gz
NINJA_TMP     := /tmp/graft_ninja
NINJA_COMMIT  := v1.12.1
NINJA_GIT_URL := https://github.com/ninja-build/ninja.git
NINJA_PATCH   := patches/ninja.patch
NINJA_OVERLAY := overlays/ninja
$(eval $(call FETCH,NINJA))

# --- link ninja's checked-in sources + our luna.cc, statically ---
NINJA_UNITS := build_log build clean clparser dyndep dyndep_parser debug_flags \
  deps_log disk_interface edit_distance eval_env graph graphviz json \
  line_printer manifest_parser metrics missing_deps parser state status \
  string_piece_util util version depfile_parser lexer subprocess-posix \
  ninja luna
NINJA_SRCS := $(addprefix $(NINJA_DIR)/src/,$(addsuffix .cc,$(NINJA_UNITS)))

$b/luna_build: $(NINJA_TGT) $(LUA_A)
	c++ -std=c++11 -O2 -static -iquote $(NINJA_DIR)/src -iquote $(LUA_DIR)/src \
	  $(NINJA_SRCS) $(LUA_A) -lpthread -o $@

.PHONY: example clean clean-all
example: $b/luna_build
	cd example && ../$b/luna_build && ./hello

clean:
	rm -rf $b

clean-all:    # also drops the download cache and bootstrapped graft
	rm -rf $b $(DL)

DIRS := $b $(DL) $(LUA_DIR) $(NINJA_DIR)
$(foreach d,$(sort $(DIRS)),$(eval $(call MK_DIR,$d)))
