#!/usr/bin/env bash
#
# Fetches the SDL2 source tree the Android build needs and copies SDL's Java
# glue (org.libsdl.app.*) into the app module so Gradle compiles it.
#
# Neither the SDL source nor the copied Java is committed (see .gitignore);
# this script makes a fresh clone reproducible. Re-running it is cheap: the
# download is skipped when SDL is already present.
#
# Usage:   android/fetch-sdl.sh            # uses the default SDL version
#          SDL2_VERSION=2.30.9 android/fetch-sdl.sh
set -euo pipefail

SDL_VERSION="${SDL2_VERSION:-2.30.9}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SDL_DIR="$HERE/SDL"

if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
    echo ">> Downloading SDL $SDL_VERSION ..."
    tmp="$(mktemp -d)"
    url="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz"
    curl -fL -o "$tmp/sdl.tar.gz" "$url"
    tar -xzf "$tmp/sdl.tar.gz" -C "$tmp"
    mkdir -p "$SDL_DIR"
    cp -a "$tmp/SDL2-${SDL_VERSION}/." "$SDL_DIR/"
    rm -rf "$tmp"
    echo ">> SDL source ready at $SDL_DIR"
else
    echo ">> SDL source already present at $SDL_DIR (skipping download)"
fi

# Copy the SDL Java glue into the app. SDLActivity and friends live in
# org.libsdl.app and are loaded/called from native code via JNI.
JAVA_SRC="$SDL_DIR/android-project/app/src/main/java/org"
JAVA_DST="$HERE/app/src/main/java/org"
if [ ! -d "$JAVA_SRC" ]; then
    echo "!! Could not find SDL Java glue at $JAVA_SRC" >&2
    exit 1
fi
rm -rf "$JAVA_DST"
mkdir -p "$(dirname "$JAVA_DST")"
cp -a "$JAVA_SRC" "$JAVA_DST"
echo ">> Copied SDL Java glue to $JAVA_DST"
echo ">> Done. You can now run: (cd android && ./gradlew assembleDebug)"
