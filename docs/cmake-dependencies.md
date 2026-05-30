# Managing C++ dependencies (CMake)

How third-party libraries are pulled into the **C++ ingest** build, and how to add
a new one — including referencing a library straight from **git**, which is the
closest CMake has to a `package.json` dependency entry.

> This documents capability. The current build is unchanged: nothing here is
> active until you add it. The default `cmake --build` behaves exactly as before.

---

## What declares dependencies today

| Dependency | Mechanism | Pinned by | Where |
|---|---|---|---|
| Apache Arrow + Parquet | `find_package(Arrow/Parquet CONFIG)` | whatever is installed (system, 24.0.0 via apt) | `CMakeLists.txt`, behind `-DSMF2PARQUET_WITH_PARQUET=ON` |
| `mf::` / `smf::` cores | **vendored** (checked-in source) | the committed copy + `vendor/mf_records/SYNC.md` | `vendor/mf_records/` |

So today there is **no fetch-and-pin manifest** for C++ (no `vcpkg.json` /
`conanfile.txt`): system libraries are *found*, and the in-house cores are
*vendored*. The three ways to bring in a library, and when to pick each:

| Approach | Use when | Reproducibility |
|---|---|---|
| `find_package(...)` | the lib is stable and available from the OS/package manager (Arrow, OpenSSL, …) | depends on what's installed |
| **FetchContent (git)** | you want a specific version built from source, pinned in-repo, no system install | high — pin `GIT_TAG` to a tag/SHA |
| **Vendoring** (`vendor/`) | you need it offline, locally modified, or it has no usable build system (header-only cores) | highest — the exact bytes are committed |

---

## Adding a dependency from git (FetchContent)

FetchContent declares a library by **git URL + pinned ref** and makes its targets
available to link. Pinning `GIT_TAG` to a release tag or commit SHA is the
equivalent of a lockfile — it makes the build reproducible.

Add to `CMakeLists.txt` (a commented skeleton already sits at the bottom of that
file):

```cmake
include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2          # PIN: release tag or full commit SHA
    GIT_SHALLOW    TRUE            # faster clone (works with tags/branches)
    FIND_PACKAGE_ARGS              # CMake >= 3.24: reuse a system copy if found
)
FetchContent_MakeAvailable(fmt)

target_link_libraries(smf2parquet PRIVATE fmt::fmt)
```

Notes:
- **Pin to an immutable ref.** A tag (`11.0.2`) or full SHA — never a moving
  branch like `main` — or builds aren't reproducible.
- **`FIND_PACKAGE_ARGS`** (CMake ≥ 3.24) lets CMake satisfy the dependency from an
  installed copy first and only clone if it's missing — best of both worlds.
- **Header-only libs** still work: many expose an `INTERFACE` target you link the
  same way; if not, add its include dir to your target.
- The source is cloned under the build dir (`build/_deps/`), so it's gitignored
  and a clean build re-fetches it.

### Offline / locked builds
```bash
# Fail instead of touching the network (must already be populated):
cmake -B build -DFETCHCONTENT_FULLY_DISCONNECTED=ON
# Point a dependency at a local checkout instead of cloning:
cmake -B build -DFETCHCONTENT_SOURCE_DIR_FMT=/path/to/local/fmt
```

---

## Vendoring instead (the existing convention)

When you'd rather commit the source (offline, modified, or no build system),
follow the established `vendor/mf_records/` pattern:

1. Copy the library under `vendor/<name>/`.
2. Keep its upstream **`LICENSE`/`NOTICE`** alongside it (license compliance).
3. Add a `SYNC.md` recording the upstream URL + exact version/commit, and a
   `sync.sh` to re-pull (mirror `vendor/mf_records/SYNC.md`).
4. Wire it into the build, e.g. for a header-only lib:
   `target_include_directories(smf2parquet PRIVATE vendor/<name>/include)`.

---

## See also
- `vendor/mf_records/SYNC.md` — the vendoring convention in practice.
- `DESIGN.md` §11 — build & dependency overview.
- CMake docs: [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html).
