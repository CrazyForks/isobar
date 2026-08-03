# Releasing Isobar

How a version gets out, and — more usefully — the mistakes that have
actually happened, so they do not happen twice.

The short version: **CI is the only thing that can see four of the five
platforms.** A release that has not been through a packaging run on a
branch is a guess.

---

## The one rule that matters

**Push `main` after every session, even with no release in sight.**

Three sessions of work once sat unpushed on a laptop. When it finally
reached CI at release time it carried a compile error that broke **both
Linux jobs** — `size_t` used unqualified in a header, which newer libstdc++
leaks through `<vector>` and gcc-11 does not. On the developer's Mac it
built perfectly every time, for three sessions running. Had the tag gone
out unchecked, v1.5.0 would have shipped with no Linux packages at all.

Nothing else in this document is as important as that sentence.

## What CI covers that a development machine cannot

`build.yml` runs on five runners: macOS Apple Silicon, macOS Intel,
Linux x86_64, Linux ARM64, and Windows. A single developer machine is one
of those five. In practice the gaps that bite are:

| Platform | What only it catches |
|---|---|
| `ubuntu-22.04` / `-arm` | gcc-11 + older libstdc++: unqualified `size_t`, missing `<cstddef>`, anything newer headers happen to pull in for you |
| `windows-latest` | MSVC `/W4`: shadowed locals (C4456), truncating casts (C4310), `fopen` deprecation (C4996) |
| Both ARM runners | float-heavy code paths — the resampler and the sync tracker — on a different FPU |

The project's standard is **zero compiler warnings on all five**. It has
been met at every release so far, and warnings are worth keeping at zero
precisely so a new one is visible the day it appears.

## Cutting a release

1. **Branch.** `release/vX.Y.Z` off `main`.
2. **Bump the version** in `CMakeLists.txt` — the `project(... VERSION ...)`
   call is the single source of truth. `cmake/isobar_version.h.in` and the
   Windows resource script are generated from it, so nothing else needs
   editing for the version to be correct in the GUI and the `.exe` metadata.
3. **Sweep the docs.** See the checklist below — the version string is the
   *least* of it.
4. **Push the branch, then dry-run both workflows:**
   ```sh
   gh workflow run build.yml   --ref release/vX.Y.Z
   gh workflow run release.yml --ref release/vX.Y.Z
   ```
   `release.yml`'s `publish` job is gated to `refs/tags/*`, so this builds
   all five packages as downloadable artifacts **without** creating a
   GitHub Release. This is the step that pays for itself.
5. **Read the logs, not just the green tick.** A run can pass while
   printing warnings that matter:
   ```sh
   gh run view <id> --log | grep -E "warning C[0-9]|\[-W"
   ```
6. **Merge to `main`** (`--no-ff`, so the release keeps its shape in the
   history), push, and check `main` itself goes green.
7. **Tag and push the tag.** That, and only that, publishes:
   ```sh
   git tag -a vX.Y.Z -m "..." && git push origin vX.Y.Z
   ```
8. **Verify the release** — five assets, correctly named, not a draft:
   ```sh
   gh release view vX.Y.Z --json assets,isDraft,isPrerelease
   ```
9. **Delete the release branch** once merged, local and remote.

A tag whose run fails can be deleted and redone, but the packages are
public the moment `publish` succeeds — so the dry run at step 4 is where
problems are supposed to be found.

## Documentation checklist

Version strings are easy and are not the point. These are the places that
go quietly stale:

- `README.md` — the **Status** line, the test count in the Build block, and
  any user-visible capability the release adds (supported input formats,
  for instance).
- `docs/README.md` — Status, and the **fixture list**, which describes what
  each committed recording exists to prove.
- `core/README.md` — the module entry for whatever changed.
- `ROADMAP.md` — a release-history entry saying what changed and *why*, and
  the CI/fixture paragraph if a fixture was added.
- `docs/02-option-a-cross-platform.md` — the test count in the tree sketch.
- `.github/workflows/build.yml` — its own header comment states the test
  count. It has been wrong before.
- `DEVIATIONS.md` — only if the change is a deliberate difference from the
  original program. New format support is not a deviation.

Counting tests: `ctest --test-dir build` prints the total.

## Things that have actually gone wrong

**Unqualified `size_t` in a header** (v1.5.0). Fixed in the `.cpp` files back
in v1.1.1; the header later grew its own uses. Both Linux jobs failed to
compile. → `#include <cstddef>` in any header that says `size_t`.

**A slash in the branch name broke packaging** (v1.5.0). `release.yml` built
package filenames straight from `github.ref_name`. On a tag that is fine; on
a branch called `release/v1.5.0` the slash turned every artifact path into a
directory that did not exist, and all five jobs failed at the rename step
*after* building, signing and packaging perfectly. Since the branch dry run
is the whole safety net, the workflow was fixed rather than the branch
renamed — the matrix now carries only a per-platform suffix and a step
composes the full name with slashes turned to dashes.

**Log noise hiding real warnings** (v1.5.0). Deprecated action versions and
`brew install` on already-current formulae produced enough output to bury two
genuine MSVC warnings. Actions are now current and the macOS step installs
only what is missing. → When the log is noisy, fix the noise *first*; you
cannot see what it is hiding until you do.

**`macos-latest` set the minimum OS too high** (v1.1.0). The runner image
moved to macOS 26, so the shipped `.app` required Tahoe. Release runners are
**pinned** (`macos-15`, `ubuntu-22.04`) for exactly this reason: the runner's
OS is the package's floor. Do not "modernise" them to `-latest`.

## Known noise that is not ours

Two things survive in a clean run and cannot be fixed from this repo:

- A git `hint:` line per job from `actions/checkout`. It contains the word
  "warning" but is not one.
- `The following taps are not trusted`, twice per macOS job — emitted by the
  genuine `brew install fltk` / `brew install rtaudio`, which really are
  absent from the runner image.

Anything else in the log is worth a look.
