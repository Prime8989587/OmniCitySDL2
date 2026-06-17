#!/usr/bin/env bash
# Build CristiVerse and assemble a portable Linux package (bundles libSDL2).
# Produces dist/CristiVerse-Linux.tar.gz
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc 2>/dev/null || echo 4)"

STAGE="dist/CristiVerse-Linux"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib"

cp build/bin/CristiVerse "$STAGE/"
cp README.md MODDING.md "$STAGE/"
cp packaging/cristiverse.cfg "$STAGE/"

# Bundle the SDL2 shared library so the package is self-contained.
SDL_LIB="$(ldd build/bin/CristiVerse | awk '/libSDL2/{print $3; exit}')"
if [ -n "${SDL_LIB:-}" ] && [ -f "$SDL_LIB" ]; then
    cp -L "$SDL_LIB" "$STAGE/lib/"
    echo "Bundled SDL2: $SDL_LIB"
else
    echo "WARNING: could not locate libSDL2; users must install it (libsdl2-2.0-0)."
fi

# Launcher that prefers the bundled lib.
cat > "$STAGE/run.sh" <<'EOF'
#!/usr/bin/env bash
HERE="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/CristiVerse" "$@"
EOF
chmod +x "$STAGE/run.sh" "$STAGE/CristiVerse"

( cd dist && tar czf CristiVerse-Linux.tar.gz CristiVerse-Linux )
echo "Created: dist/CristiVerse-Linux.tar.gz"
