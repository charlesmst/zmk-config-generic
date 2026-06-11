#!/usr/bin/env bash
# Build script for roBakesb — Damex ESB dongle setup.
# Uses the west workspace rooted in this repository.
#
# Usage:
#   ./local_build_roba_kesb.sh [artifact-name]   # build one target
#   ./local_build_roba_kesb.sh                   # build all targets
#   ./local_build_roba_kesb.sh --fresh <name>    # force cmake reconfigure

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="${WORKSPACE:-$REPO_DIR}"
ZMK_APP="$WORKSPACE/zmk/app"
ZMK_CONFIG="$REPO_DIR/config"
OUTPUT_DIR="$REPO_DIR/firmware"

if [[ -d "$HOME/.local/opt/zephyr-sdk-0.17.0" ]]; then
    ZEPHYR_SDK_INSTALL_DIR="$HOME/.local/opt/zephyr-sdk-0.17.0"
else
    ZEPHYR_SDK_INSTALL_DIR="$HOME/.local/opt/zephyr-sdk-0.16.8"
    echo "⚠  Zephyr SDK 0.17.0 not found; using 0.16.8"
fi

EXTRA_MODULES=(
    "$ZMK_APP/module"
    "$ZMK_APP/keymap-module"
    "$WORKSPACE/modules/zmk/tri-state"
    "$WORKSPACE/zmk-feature-split-esb"
    "$WORKSPACE/zmk-pmw3610-driver"
    "$WORKSPACE/zmk-paw3395-driver"
    "$WORKSPACE/zmk-vfx-rgbled-indicator"
    "$WORKSPACE/zmk-behavior-stepped-scroll"
    "$REPO_DIR"
)

EXTRA_MODULES_ARG=$(IFS=';'; echo "${EXTRA_MODULES[*]}")

DOWNLOADS="/mnt/c/Users/charl/Downloads"
FRESH=0
TARGET=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh) FRESH=1; shift ;;
        *)       TARGET="$1"; shift ;;
    esac
done

export ZEPHYR_BASE="$WORKSPACE/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR

mkdir -p "$OUTPUT_DIR"

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
    # only kesb targets
    if "roBakesb" not in shield and "roBakesb" not in name:
        continue
    if filter_name and name != filter_name:
        continue
    results.append({"name": name, "board": board, "shield": shield,
                     "cmake_args": cmake, "snippet": snippet})

if filter_name and not results:
    print(f"ERROR: artifact-name '{filter_name}' not found in build.yaml (kesb targets only)", file=sys.stderr)
    sys.exit(1)

print(json.dumps(results))
PYEOF
}

build_target() {
    local name="$1" board="$2" shield="$3" cmake_args="$4" snippet="$5"
    local build_dir="$WORKSPACE/build_roba_kesb/$name"

    echo ""
    echo "========================================="
    echo "  Building: $name"
    echo "  Board:    $board"
    echo "  Shield:   $shield"
    [[ -n "$snippet" ]] && echo "  Snippet:  $snippet"
    echo "  SDK:      $ZEPHYR_SDK_INSTALL_DIR"
    echo "========================================="

    if [[ "$FRESH" -eq 1 || ! -d "$build_dir" ]]; then
        echo "→ Configuring (fresh)..."
        rm -rf "$build_dir"
        local snippet_args=()
        if [[ -n "$snippet" ]]; then
            snippet_args+=(-DSNIPPET="$(echo "$snippet" | tr ' ' ';')")
        fi
        cmake -B "$build_dir" -S "$ZMK_APP" \
            -GNinja \
            -DZephyr_DIR="$WORKSPACE/zephyr/share/zephyr-package/cmake" \
            -DBOARD="$board" \
            -DSHIELD="$shield" \
            -DZMK_CONFIG="$ZMK_CONFIG" \
            -DEXTRA_ZEPHYR_MODULES="$EXTRA_MODULES_ARG" \
            "${snippet_args[@]}" \
            $cmake_args
    else
        echo "→ Incremental build (use --fresh to reconfigure)..."
    fi

    cmake --build "$build_dir"

    local uf2="$build_dir/zephyr/zmk.uf2"
    if [[ -f "$uf2" ]]; then
        cp "$uf2" "$OUTPUT_DIR/$name.uf2"
        echo "✓ $OUTPUT_DIR/$name.uf2"
        if [[ -d "$DOWNLOADS" ]]; then cp "$uf2" "$DOWNLOADS/$name.uf2" && echo "✓ $DOWNLOADS/$name.uf2"; fi
    elif [[ -f "$build_dir/zephyr/zmk.hex" ]]; then
        local uf2conv="$WORKSPACE/zephyr/scripts/build/uf2conv.py"
        if [[ "$board" == *"nrf52840"* && -f "$uf2conv" ]]; then
            python3 "$uf2conv" -c -f NRF52840 \
                -o "$OUTPUT_DIR/$name.uf2" "$build_dir/zephyr/zmk.hex"
            echo "✓ $OUTPUT_DIR/$name.uf2"
            if [[ -d "$DOWNLOADS" ]]; then
                cp "$OUTPUT_DIR/$name.uf2" "$DOWNLOADS/$name.uf2"
                echo "✓ $DOWNLOADS/$name.uf2"
            fi
        fi
        cp "$build_dir/zephyr/zmk.hex" "$OUTPUT_DIR/$name.hex"
        echo "✓ $OUTPUT_DIR/$name.hex"
    else
        echo "✗ No output at $build_dir/zephyr/"
        return 1
    fi
}

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
    echo "All roBakesb builds succeeded."
fi
