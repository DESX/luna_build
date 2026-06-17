# Luna

**Hermetic builds for C++. Dependencies that just work.**

C++ dependency management is broken. Rust has Cargo; C++ has vcpkg/Conan/CMake
glue that works on your machine and breaks in CI. The goal of Luna is simple:

```lua
-- build.luna
local cpp = require("luna.cpp")

cpp.binary {
  name = "myapp",
  srcs = glob("src/**/*.cpp"),
  deps = {"fmt", "nlohmann_json", "spdlog"},   -- fetched, hashed, built, cached
}
```

```sh
luna_build        # works identically on every machine, forever
```

That is the destination. This document also describes exactly **what exists
today** and how the rest gets built on top of it.

## The strategy: don't reinvent the engine

A build system is two things:

1. **An execution engine** — turn a dependency graph into commands, run them in
   parallel, do correct incremental rebuilds. Ninja already does this better
   than almost anything, in a single static binary.
2. **A frontend** — how humans *describe* the build.

Ninja nails (1) but its frontend is a static, non-programmable text format
(`build.ninja`) that nobody writes by hand — you need a generator (CMake, Meson,
gn) on top. Luna's bet: **keep ninja's engine, replace its frontend with a real
programming language (Lua).** Once the build description is a program, the
entire hermetic-dependency vision above becomes *ordinary Lua libraries* that
emit build rules — no new engine required.

```
   Vision        cpp.binary / deps={...} / content-addressed cache   ← Lua modules (roadmap)
   ───────────────────────────────────────────────────────────────
   Foundation    build.luna  →  Lua  →  ninja manifest  →  ninja engine   ← BUILT
```

You cannot build `cpp.binary` on top of static `build.ninja` files. You can
build it on top of a Lua-driven ninja. That foundation is what we've built
first.

## What exists today: the Lua-driven ninja core

`luna_build` is a single, statically-linked binary that is a **drop-in
replacement for ninja** — same flags, same engine, same incremental builds,
same tools (`-t targets`, `-t clean`, …). The only difference: it reads
`build.luna` (a Lua program) instead of `build.ninja`.

```lua
-- build.luna — the lowest layer, equivalent to hand-written ninja
rule("cc", {
  command = "cc $cflags -c $in -o $out",
  description = "CC $out",
})
rule("link", { command = "cc $in -o $out", description = "LINK $out" })

variable("cflags", "-O2 -Wall")

build("hello.o", "cc", "hello.c")
build("hello", "link", "hello.o")

default("hello")
```

```sh
$ luna_build
[1/2] CC hello.o
[2/2] LINK hello
```

Because the frontend is now a real language, even at this low level you get
what plain ninja syntax can't express:

```lua
-- one rule, many targets
local objs = {}
for _, name in ipairs({"parser", "lexer", "eval", "main"}) do
  build(name .. ".o", "cc", "src/" .. name .. ".c")
  objs[#objs + 1] = name .. ".o"
end
build("app", "link", objs)
default("app")
```

## How the core is built (minimal patch + overlay)

Luna Build is **not a fork of ninja**. It is upstream ninja plus Lua, assembled
with [graft](../graft) using the smallest possible set of changes:

```
            ┌──────────────┐   patch: src/ninja.cc (4 hunks)
   ninja ───┤   graft       ├── overlay: src/luna.{cc,h}  ──┐
            │   FETCH       │                                │   static link
   lua ─────┤               ├── make liblua.a ──────────────┼──► luna_build
            └──────────────┘                                │
```

The key trick keeps the new code small: instead of reimplementing ninja's graph
builder against a Lua API, the `.luna` file's `rule`/`build`/… calls **emit
ordinary ninja manifest text**, which is handed straight to ninja's own
`ManifestParser`. All of ninja's parsing, evaluation, dependency graph, and
scheduling logic is reused unchanged.

| Piece | What it is | Size |
|-------|------------|------|
| `patches/ninja.patch` | the *only* edit to ninja's code — default file → `build.luna`, route loading through `LunaLoad()` | 4 hunks, 1 file |
| `overlays/ninja/src/luna.cc` | Lua interpreter + a Lua prelude mapping the API to ninja text | ~130 lines |
| `overlays/ninja/src/luna.h` | one-function header | ~25 lines |
| `Makefile` | graft wiring + static link | ~70 lines |

Nothing else in ninja is touched. To bump ninja, change `NINJA_COMMIT` in the
`Makefile`; the 4-hunk patch re-targets trivially (`make ninja_patch`
regenerates it against a fresh checkout).

## Building

Requirements: GNU Make, a C/C++ toolchain, `curl`, `git`, `tar`. [graft](https://github.com/DESX/graft)
is self-bootstrapped by the `Makefile` at a pinned release tag — no manual setup.

```sh
make            # bootstrap graft, fetch ninja + lua, patch/overlay, statically link
./build/luna_build --version      # 1.12.1  (the ninja core version)

make example    # build and run the bundled example project
```

The result is `build/luna_build` — one static binary, no runtime dependencies:

```sh
$ ldd build/luna_build
        not a dynamic executable
```

## The `.luna` API (core layer)

A `build.luna` file is a Lua script. These globals each generate the
corresponding ninja construct. Higher-level modules (see Roadmap) will be Lua
libraries that ultimately call these.

### `rule(name, opts)`

```lua
rule("cc", {
  command = "cc $cflags -c $in -o $out",   -- required
  description = "CC $out",
  depfile = "$out.d",
  deps = "gcc",                            -- "gcc" or "msvc"
  -- generator, restat, rspfile, rspfile_content, pool, ... all supported
})
```

Any key/value becomes a `key = value` line in the rule. `$in`, `$out`, and
custom `$vars` work exactly as in ninja.

### `build(...)`

Simple positional, or the full table form for the rest of ninja's edge syntax:

```lua
build("hello.o", "cc", "hello.c")          -- outputs, rule, inputs

build {
  outputs = {"a.o", "b.o"},                -- string or list
  rule = "cc",
  inputs = {"a.c", "b.c"},
  implicit = "config.h",                   -- | implicit deps
  order_only = "gen_headers",              -- || order-only deps
  implicit_outputs = "a.log",              -- | implicit outputs
  validations = "checkstamp",              -- |@ validations
  vars = { cflags = "-O0" },               -- per-edge variable overrides
}
```

### `variable(name, value)` (alias `var`), `default(...)`, `pool(name, opts)`

```lua
variable("cflags", "-O2 -Wall")            -- top-level ninja binding
default("hello")                           -- or default("a", "b") / default({...})
pool("link_pool", { depth = 4 })
```

Plus everything Lua gives you: variables, functions, loops, `require`, string
handling, and the standard library.

## Roadmap: from core to vision

Each layer is a Lua library that emits the rules/builds of the layer below.
None of it requires touching the engine again.

- ⬜ **`luna.cpp` module** — `cpp.binary` / `cpp.library` / `cpp.test`: expand a
  high-level target into `cc`/`link` rules and `build` edges.
- ⬜ **Dependency resolution** — `deps = {"fmt", ...}` resolved against a signed
  manifest of known libraries; custom deps by URL + SHA256.
- ⬜ **Content-addressed cache** — every dependency, toolchain, and artifact keyed
  by hash; reproducible, MITM-proof, offline-capable.
- ⬜ **Hermetic toolchains** — pinned Clang/GCC fetched and verified, so builds are
  identical across machines.
- ⬜ **Patching without forking** — local patches applied to hash-pinned deps at
  build time (graft already models this; surface it in `.luna`).
- ⬜ **`subninja`/`include` equivalents** — Lua `dofile`/`require` partly cover this.

## Status

Working prototype of the foundation. Built on ninja `v1.12.1` + Lua `5.4.7`.

- ✅ Static single binary; full ninja CLI and tools
- ✅ `rule` / `build` / `variable` / `default` / `pool`
- ✅ Implicit / order-only / validation deps, per-edge vars, `depfile`/`deps`
- ⬜ The dependency-management layers above (Roadmap)

## Layout

```
luna_build/
├── Makefile                      # graft-driven build
├── patches/ninja.patch           # the only edit to ninja's own code
├── overlays/ninja/src/luna.{cc,h}# Lua frontend (new files)
├── example/{build.luna,hello.c}  # sample build
└── build/                        # generated: ninja/, lua/, luna_build
```

## License

The Luna glue (Makefile, `luna.cc`, `luna.h`, patches) is MIT.
Ninja is Apache-2.0; Lua is MIT. Each retains its own license.
