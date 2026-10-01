# Releases

gtube is released as prebuilt archives on GitHub Releases, built and published by `.github/workflows/publish.yml` from a `v*` tag.

## Targets

| Target | Zig target | Archive |
|---|---|---|
| Linux x86-64, glibc 2.27 or newer | `x86_64-linux-gnu.2.27` | `gtube-vX.Y.Z-x86_64-linux.tar.gz` |
| Windows x86-64 | `x86_64-windows-gnu` | `gtube-vX.Y.Z-x86_64-windows.zip` |

Both are built on Linux with Zig 0.16.0. The Linux floor is glibc 2.27 (Ubuntu 18.04); older glibc lacks `memfd_create`, which SDL and the generated Bend runtime use. SDL's build also links `posix_spawn_file_actions_addchdir_np`, which glibc added in 2.29, so builds for an older glibc compile `src/glibc_compat.c` in its place. It calls the running glibc's own function when there is one, so the release loses nothing on newer systems, and reports `ENOSYS` on glibc 2.27 and 2.28, where a process can only start in the current directory; gtube never asks for another. To check the floor, run the Linux archive in `docker.io/library/ubuntu:18.04`.

Each archive holds the executable, its `.gpu` file, `README.md`, `LICENSE`, and a `licenses/` folder with the license texts of everything compiled in: the generated Bend runtime (Apache-2.0), gesso (MIT), stb, Opus (BSD-3-Clause), SDL3 with its REUSE license set, and on Linux SDL's Wayland protocol code, or on Windows the WebView2 headers. `release/stage.sh` builds one target and packs it:

```sh
release/stage.sh linux dist
release/stage.sh windows dist
```

## Checks

`validation.yml` builds both archives, starts the Linux build and renders a frame headless (`--demo --shot`), then does the same with the Windows build on a Windows runner. Its gate job, `validate`, runs even when a build is skipped and passes only when both succeeded (`wakamex/release-actions/validate-gate`); it is the required check `release-eligible / validate`, produced on every push to `main` by `release-eligibility.yml`.

## Releasing vX.Y.Z

1. Set `.version` in `build.zig.zon` to `X.Y.Z` and write `release-notes/vX.Y.Z.md`.
2. Commit both as `Release vX.Y.Z` and push `main` without tags.
3. Wait for `release-eligible / validate` to pass on that commit, and check that remote `main` still points to it.
4. Tag it with an annotated `vX.Y.Z` and push only the tag.

`publish.yml` then reruns validation, checks that the tag is annotated, matches `build.zig.zon` and has release notes (`wakamex/release-actions/verify-release-tag`), and rebuilds both archives from the tag. The shared `wakamex/release-actions` binary release workflow writes `SHA256SUMS`, attests every asset and verifies the attestations, and creates the Release from the notes file. Rerunning it on a published tag verifies the existing Release instead of replacing it. A broken release is fixed by a new version; tags and published assets are never moved or replaced.

The attestations are signed by the shared workflow, so verify a downloaded asset with:

```sh
gh attestation verify ASSET --repo wakamex/gtube \
  --signer-workflow wakamex/release-actions/.github/workflows/binary-release.yml
gh release verify-asset vX.Y.Z ASSET --repo wakamex/gtube
```

## One-time setup

- Turn on immutable releases in the repository settings.
- Once `release-eligible / validate` has passed on `main`, add a tag ruleset named `Validated release tags` for `refs/tags/v*` that requires that check (from the GitHub Actions app that produced it), with no bypass actors, deletion restricted and non-fast-forward updates blocked.
