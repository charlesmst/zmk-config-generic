#!/usr/bin/env bash
# Local build script for zmk-config-generic.
# Uses the pre-configured west workspace at WORKSPACE instead of Docker,
# so incremental builds are fast and local fork changes are picked up.
#
# Usage:
#   ./local_build.sh [artifact-name]   # build one target from build.yaml
#   ./local_build.sh                   # build all targets in build.yaml
#   ./local_build.sh --fresh <name>    # force cmake reconfigure then build
#
# Outputs go to ./firmware/<artifact-name>.uf2
# If DOWNLOADS is set and the path exists, also copied there.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="${WORKSPACE:-$HOME/personal/zmk-config-roBa-charybdis-esb}"
ZMK_APP="$WORKSPACE/zmk/app"
ZMK_CONFIG="$REPO_DIR/config"
OUTPUT_DIR="$REPO_DIR/firmware"
DOWNLOADS="/mnt/c/Users/charl/Downloads"

FRESH=0
TARGET=""

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh) FRESH=1; shift ;;
        *)       TARGET="$1"; shift ;;
    esac
done

# Activate Zephyr env (zephyr-sdk sets up the toolchain)
export ZEPHYR_BASE="$WORKSPACE/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
# Pick up the zephyr-sdk cmake packages
ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/.local/opt/zephyr-sdk-0.16.8}"
export ZEPHYR_SDK_INSTALL_DIR
cmake_extra_env=(
    -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
)

mkdir -p "$OUTPUT_DIR"

# ---------------------------------------------------------------------------
# Parse build.yaml into a list of targets using Python
# ---------------------------------------------------------------------------
read_targets() {
    python3 - "$REPO_DIR/build.yaml" "$TARGET" << 'PYEOF'
import sys, yaml, json

build_yaml = sys.argv[1]
filter_name = sys.argv[2] if len(sys.argv) > 2 else ""

with open(build_yaml) as f:
    data = yaml.safe_load(f)

targets = data.get("include", [])
results = []
for t in targets:
    board   = t.get("board", "")
    shield  = t.get("shield", "")
    name    = t.get("artifact-name", shield.split()[0])
    cmake   = t.get("cmake-args", "")
    snippet = t.get("snippet", "")
    if filter_name and name != filter_name:
        continue
    results.append({"name": name, "board": board, "shield": shield,
                     "cmake_args": cmake, "snippet": snippet})

if filter_name and not results:
    print(f"ERROR: artifact-name '{filter_name}' not found in build.yaml", file=sys.stderr)
    sys.exit(1)

print(json.dumps(results))
PYEOF
}

# ---------------------------------------------------------------------------
# Build one target
# ---------------------------------------------------------------------------
build_target() {
    local name="$1"
    local board="$2"
    local shield="$3"
    local cmake_args="$4"
    local snippet="$5"

    local build_dir="$WORKSPACE/build/$name"
    echo ""
    echo "========================================="
    echo "  Building: $name"
    echo "  Board:    $board"
    echo "  Shield:   $shield"
    [[ -n "$cmake_args" ]] && echo "  CMake:    $cmake_args"
    [[ -n "$snippet" ]]    && echo "  Snippet:  $snippet"
    echo "========================================="

    if [[ "$FRESH" -eq 1 || ! -d "$build_dir" ]]; then
        echo "→ Configuring (fresh)..."
        rm -rf "$build_dir"
        local snippet_args=()
        if [[ -n "$snippet" ]]; then
            local snippet_list
            snippet_list=$(echo "$snippet" | tr ' ' ';')
            snippet_args+=(-DSNIPPET="$snippet_list")
        fi
        # shellcheck disable=SC2086
        cmake -B "$build_dir" -S "$ZMK_APP" \
            -GNinja \
            -DBOARD="$board" \
            -DSHIELD="$shield" \
            -DZMK_CONFIG="$ZMK_CONFIG" \
            "${snippet_args[@]}" \
            "${cmake_extra_env[@]}" \
            $cmake_args
    else
        echo "→ Incremental build (use --fresh to reconfigure)..."
    fi

    cmake --build "$build_dir"

    local uf2="$build_dir/zephyr/zmk.uf2"
    local bin="$build_dir/zephyr/zmk.bin"
    if [[ -f "$uf2" ]]; then
        cp "$uf2" "$OUTPUT_DIR/$name.uf2"
        echo "✓ $OUTPUT_DIR/$name.uf2"
        if [[ -d "$DOWNLOADS" ]]; then
            cp "$uf2" "$DOWNLOADS/$name.uf2"
            echo "✓ $DOWNLOADS/$name.uf2"
        fi
    elif [[ -f "$build_dir/zephyr/zmk.hex" ]]; then
        local hex="$build_dir/zephyr/zmk.hex"
        cp "$hex" "$OUTPUT_DIR/$name.hex"
        echo "✓ $OUTPUT_DIR/$name.hex"
        if [[ -d "$DOWNLOADS" ]]; then
            cp "$hex" "$DOWNLOADS/$name.hex"
            echo "✓ $DOWNLOADS/$name.hex"
        fi
    elif [[ -f "$bin" ]]; then
        cp "$bin" "$OUTPUT_DIR/$name.bin"
        echo "✓ $OUTPUT_DIR/$name.bin"
        if [[ -d "$DOWNLOADS" ]]; then
            cp "$bin" "$DOWNLOADS/$name.bin"
            echo "✓ $DOWNLOADS/$name.bin"
        fi
    else
        echo "✗ No .uf2, .hex, or .bin found at $build_dir/zephyr/"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
targets_json="$(read_targets)"

failed=()
while IFS= read -r entry; do
    name=$(echo "$entry"       | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['name'])")
    board=$(echo "$entry"      | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['board'])")
    shield=$(echo "$entry"     | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['shield'])")
    cmake_args=$(echo "$entry" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['cmake_args'])")
    snippet=$(echo "$entry"    | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['snippet'])")

    if ! build_target "$name" "$board" "$shield" "$cmake_args" "$snippet"; then
        failed+=("$name")
    fi
done < <(echo "$targets_json" | python3 -c "
import sys, json
for item in json.load(sys.stdin):
    print(json.dumps(item))
")

echo ""
if [[ ${#failed[@]} -gt 0 ]]; then
    echo "FAILED: ${failed[*]}"
    exit 1
else
    echo "All builds succeeded."
fi
