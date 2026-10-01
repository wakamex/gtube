#!/usr/bin/env bash
# Builds one release target and packs it with the license texts of everything compiled into it.
#   release/stage.sh linux|windows OUT_DIR
# Writes OUT_DIR/gtube-vVERSION-x86_64-linux.tar.gz or OUT_DIR/gtube-vVERSION-x86_64-windows.zip,
# VERSION being build.zig.zon's .version. Run from a clean checkout; zig 0.16 fetches the
# dependencies into zig-pkg/, where their license texts are read from.
set -euo pipefail
cd "$(dirname "$0")/.."

target=${1:?linux or windows}
out=$(mkdir -p "${2:?output directory}" && cd "$2" && pwd)
case $target in
    linux) zig_target=x86_64-linux-gnu.2.27 exe=gtube ext=tar.gz ;;
    windows) zig_target=x86_64-windows-gnu exe=gtube.exe ext=zip ;;
    *) echo "unknown target: $target" >&2; exit 1 ;;
esac
version=$(sed -n 's/^ *\.version = "\(.*\)",$/\1/p' build.zig.zon)
test -n "$version"
name=gtube-v$version-x86_64-$target

# A dependency's package folder, from the .hash that a build.zig.zon gives it.
package() {
    local hash
    hash=$(grep -A3 "^ *\.$2 = \.{" "$1" | sed -n 's/^ *\.hash = "\(.*\)",$/\1/p')
    test -n "$hash" && test -d "zig-pkg/$hash" || { echo "no package for $2 in $1" >&2; exit 1; }
    echo "zig-pkg/$hash"
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
zig build --release=fast -Dtarget="$zig_target" -p "$work/prefix"

stage=$work/$name
mkdir -p "$stage/licenses"
cp "$work/prefix/bin/$exe" "$work/prefix/bin/$exe.gpu" README.md LICENSE "$stage/"
gesso=$(package build.zig.zon gesso)
sdl=$(package "$gesso/build.zig.zon" sdl)
opus=$(package "$gesso/build.zig.zon" opus)
cp src/bend/LICENSE "$stage/licenses/bend-runtime-LICENSE"
cp "$gesso/LICENSE" "$stage/licenses/gesso-LICENSE"
cp "$gesso/vendor/stb/LICENSE" "$stage/licenses/stb-LICENSE"
cp "$opus/COPYING" "$stage/licenses/opus-COPYING"
mkdir "$stage/licenses/SDL"
cp -r "$sdl/LICENSE.txt" "$sdl/REUSE.toml" "$sdl/LICENSES" "$stage/licenses/SDL/"
if [ "$target" = linux ]; then  # SDL's Wayland protocol code, compiled in on Linux only
    linux_deps=$(package "$sdl/build.zig.zon" sdl_linux_deps)
    mkdir "$stage/licenses/SDL_linux_deps"
    cp -r "$linux_deps/LICENSE.txt" "$linux_deps/REUSE.toml" "$linux_deps/LICENSES" "$stage/licenses/SDL_linux_deps/"
else  # the WebView2 headers, for the sign-in window
    cp src/vendor/webview2/LICENSE.txt "$stage/licenses/webview2-LICENSE.txt"
fi

cd "$work"
rm -f "$out/$name.$ext"
if [ "$ext" = zip ]; then zip -qrX "$out/$name.$ext" "$name"; else tar -czf "$out/$name.$ext" "$name"; fi
echo "$out/$name.$ext"
