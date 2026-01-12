# Luna

Hermetic builds for C++. Dependencies that just work.

## The Problem

```cpp
// What you want to write
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <httplib.h>

int main() {
    auto config = nlohmann::json::parse(read_file("config.json"));
    fmt::print("Starting server on port {}\n", config["port"]);
    httplib::Server server;
    // ...
}
```

```
// What you actually deal with
- vcpkg? conan? system packages? git submodules?
- Works on your machine, breaks in CI
- New team member spends 2 days setting up dependencies
- Upstream released a breaking change, your build is broken
- "Just use CMake" — but CMake is the problem
```

C++ dependency management is broken. Rust solved this with Cargo. C++ has nothing.

## The Solution

```python
# build.luna
load("luna.dev/cpp.star", "cpp_binary")
load("luna.dev/toolchains/clang.star", "clang")

clang(version = "17")

cpp_binary(
    name = "myapp",
    deps = ["fmt", "nlohmann_json", "cpp-httplib"],
)
```

```sh
luna build
```

That's it. Works identically on every machine, forever.

- First build fetches dependencies (cached by content hash)
- Subsequent builds use cache
- Same binary on Linux, macOS, Windows
- New team member clones repo, runs `luna build`, done

## How It Works

### Content-Addressed Everything

Every module, every dependency, every toolchain is identified by its SHA256 hash:

```python
load(
    "luna.dev/cpp.star",
    "cpp_binary",
    sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
)
```

The URL is how you discover it. The hash is what it is. Once cached, the URL is irrelevant.

**Implications:**
- Original repo could be deleted—doesn't matter, you have the hash
- No MITM attacks—content is verified
- Builds are reproducible forever
- Your cache IS your copy—no need to fork/mirror dependencies

### Standard Library (No Manual Hashes)

For common dependencies, Luna ships a signed manifest. You write:

```python
deps = ["fmt", "nlohmann_json", "boost_asio"]
```

Luna looks up the hashes internally. The manifest is signed and versioned with Luna releases. You trust Luna, not individual URLs.

For custom dependencies, you provide the hash explicitly or let Luna generate a lockfile.

### Patching Without Forking

Found a bug in a dependency? Don't fork the entire repo:

```python
cpp_binary(
    name = "myapp",
    deps = [
        dep("openssl", patches = ["./patches/fix-arm64.patch"]),
        "fmt",
    ],
)
```

The base dependency is immutable (hash-verified). Your patch is local, version-controlled, applied at build time. When upstream fixes the bug, remove your patch.

### Offline Builds

For air-gapped environments:

```sh
# On a machine with internet
luna cache export -o deps.tar.gz

# Transport to secure facility
luna cache import deps.tar.gz
luna build --offline
```

The exported cache contains everything needed to build. No network access required.

### Version From Git

```python
load("luna.dev/version.star", "git_version")

cpp_binary(
    name = "myapp",
    version = git_version(),  # "1.2.3" from tag, or "1.2.3-7-gabc1234"
)
```

Versions derived from git tags. Tagged commits get clean versions. Development builds include commit distance and hash.

## Modules Have Opinions, Luna Doesn't

Luna core is minimal:
- Execute a dependency graph correctly and in parallel
- Fetch modules by URL, verify by hash
- Provide primitives to Starlark (file I/O, shell, glob, git)

That's it. Luna has no opinions about:
- Project structure
- Compilers or toolchains
- How to build C++ (or anything else)
- Dependency resolution strategies

**All opinions come from modules.** The `cpp.star` module has opinions about C++ builds. The `clang.star` module has opinions about Clang. You choose which opinions to adopt.

Don't like how `cpp.star` handles something? Change it:

```sh
luna inspect cpp.star        # See exactly what it does
luna fork cpp.star           # Copy to ./modules/cpp.star
# Edit to your liking
```

Modules are simple Starlark files, not black boxes.

## The Module Ecosystem

### Standard Modules (Maintained by Luna)

```
luna.dev/
├── cpp.star              # cpp_binary, cpp_library, cpp_test
├── toolchains/
│   ├── clang.star        # Hermetic Clang
│   ├── gcc.star          # Hermetic GCC
│   └── msvc.star         # MSVC detection
└── version.star          # Git-based versioning
```

### Curated Libraries

```
luna.dev/libs/
├── fmt.star              # {fmt} formatting library
├── json.star             # nlohmann/json
├── spdlog.star           # Fast logging
├── catch2.star           # Testing framework
├── boost/                # Boost libraries (individual modules)
├── openssl.star          # OpenSSL (wraps complex build)
└── ...                   # ~100 common libraries
```

Each library module:
- Fetches source by hash
- Builds hermetically
- Works on all platforms
- Is readable and forkable

### Community Modules

Anyone can publish a module:

1. Write a `.star` file
2. Host it anywhere (GitHub, your server, etc.)
3. Share the URL

No registry. No accounts. No gatekeepers.

## Example: Full Project

**Repository structure:**

```
myapp/
├── build.luna
├── src/
│   ├── main.cpp
│   └── server.cpp
├── include/
│   └── server.hpp
├── tests/
│   └── test_server.cpp
└── patches/
    └── openssl-fix.patch
```

**build.luna:**

```python
load("luna.dev/cpp.star", "cpp_binary", "cpp_test")
load("luna.dev/toolchains/clang.star", "clang")
load("luna.dev/version.star", "git_version")

clang(version = "17")

cpp_binary(
    name = "myapp",
    version = git_version(),
    srcs = glob("src/**/*.cpp"),
    hdrs = glob("include/**/*.hpp"),
    deps = [
        "fmt",
        "nlohmann_json",
        "spdlog",
        dep("openssl", patches = ["patches/openssl-fix.patch"]),
    ],
)

cpp_test(
    name = "tests",
    srcs = glob("tests/**/*.cpp"),
    deps = [":myapp", "catch2"],
)
```

**Commands:**

```sh
luna build          # Build myapp
luna test           # Run tests
luna build --release # Optimized build
luna clean          # Remove build artifacts
luna cache status   # Show cached dependencies
```

## Architecture

### Luna Core (Go)

```
luna
├── Starlark interpreter (starlark-go)
├── Module fetcher (URL + SHA256 verification)
├── Content-addressed cache
├── DAG builder
├── Parallel executor
└── CLI
```

### Primitives Exposed to Starlark

```python
# File operations
read(path)
write(path, content)
glob(pattern)

# Execution
sh(command)
env(name)

# Network (cached)
fetch(url, sha256)

# Git
git.describe()
git.tags()
git.sha()

# Recipes
recipes.add(
    alias = "build",
    inputs = [...],
    outputs = [...],
    run = "...",
)
```

### Extensibility

Luna's primitives are low-level enough that you could theoretically implement:
- A full Cargo-compatible resolver for Rust
- A Go modules implementation
- A Python package manager

This is out of scope for v1.0, but the architecture doesn't prevent it.

## Why Luna?

### vs. CMake + vcpkg/conan

| | Luna | CMake + vcpkg |
|---|------|---------------|
| Setup time | `curl \| sh` | Install CMake, vcpkg, configure toolchain file, etc. |
| New machine | `luna build` works | Hours of environment setup |
| Hermetic | Yes | No (system deps leak in) |
| Reproducible | Hash-verified | Best effort |
| Readable | 10-line build.luna | Hundreds of lines of CMake |

### vs. Bazel

| | Luna | Bazel |
|---|------|-------|
| Binary | Single static binary | JVM + multiple components |
| Learning curve | Familiar Python-like syntax | Steep |
| Setup | Download and run | Complex workspace setup |
| Focus | Get things done | Google-scale correctness |

### vs. Just Using Cargo/Go

Cargo and Go have great tooling. If you're writing pure Rust or pure Go, use them.

Luna is for:
- C++ projects (no good solution exists)
- Polyglot projects (C++ core, Python bindings, etc.)
- Projects that need hermetic builds without Bazel's complexity

## Installation

```sh
# Linux/macOS
curl -fsSL https://luna.dev/install.sh | sh

# Windows
irm https://luna.dev/install.ps1 | iex

# Or download directly
wget https://github.com/example/luna/releases/latest/download/luna-$(uname -s)-$(uname -m)
chmod +x luna-*
mv luna-* /usr/local/bin/luna
```

Single binary. No dependencies. No runtime.

## Status

Luna is in early development. The current focus is:

1. Core runtime (Starlark + DAG execution)
2. C++ module with hermetic Clang
3. Initial library set (~30 common C++ libraries)
4. Linux and macOS support (Windows to follow)

## License

MIT
