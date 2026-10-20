#!/usr/bin/env bash
# Build script for the standalone Keychron B1 Pro on upstream ZMK.
#
# Builds keychron-config/build.yaml against keychron-config/west.yml -- stock
# zmkfirmware/zmk, none of the roBakesb modules -- the same way the `keychron`
# CI job does. It needs its own west workspace for that, kept outside the repo
# so every worktree shares it, and created on first run.
#
# Usage:
#   ./local_build_keychron.sh [artifact-name]   # build one target
#   ./local_build_keychron.sh                   # build all targets
#   ./local_build_keychron.sh --fresh <name>    # force cmake reconfigure

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZMK_CONFIG="$REPO_DIR/keychron-config"
OUTPUT_DIR="$REPO_DIR/firmware"

# The upstream-ZMK workspace. On first run it is cloned with the roBakesb
# workspace as a path cache, so zephyr and the HALs come from local disk; point
# PATH_CACHE at any other west workspace to do the same.
KEYCHRON_WS="${KEYCHRON_WS:-$HOME/personal/zmk-upstream-ws}"
PATH_CACHE="${PATH_CACHE:-$HOME/personal/zmk-config-generic}"

if [[ -d "$HOME/.local/opt/zephyr-sdk-0.17.0" ]]; then
    ZEPHYR_SDK_INSTALL_DIR="$HOME/.local/opt/zephyr-sdk-0.17.0"
else
    ZEPHYR_SDK_INSTALL_DIR="$HOME/.local/opt/zephyr-sdk-0.16.8"
    echo "⚠  Zephyr SDK 0.17.0 not found; using 0.16.8"
fi

DOWNLOADS="${DOWNLOADS:-/mnt/c/Users/charl/Downloads}"
BUILD_ROOT="${BUILD_ROOT:-$KEYCHRON_WS/build}"
FRESH=0
TARGET=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh) FRESH=1; shift ;;
        *)       TARGET="$1"; shift ;;
    esac
done

# The ZMK revision keychron-config/west.yml pins. One source of truth: the
# workspace is moved to it whenever the two disagree.
ZMK_REV=$(python3 -c '
import sys, yaml
m = yaml.safe_load(open(sys.argv[1]))["manifest"]
print(next(p["revision"] for p in m["projects"] if p["name"] == "zmk"))
' "$ZMK_CONFIG/west.yml")

sync_workspace() {
    local update=0
    if [[ ! -d "$KEYCHRON_WS/.west" ]]; then
        echo "→ Creating upstream ZMK workspace at $KEYCHRON_WS"
        west init -m https://github.com/zmkfirmware/zmk --mr main --mf app/west.yml "$KEYCHRON_WS"
        update=1
    fi
    if [[ "$(git -C "$KEYCHRON_WS/zmk" rev-parse HEAD)" != "$ZMK_REV" ]]; then
        echo "→ Moving zmk to $ZMK_REV"
        git -C "$KEYCHRON_WS/zmk" fetch origin "$ZMK_REV"
        git -C "$KEYCHRON_WS/zmk" checkout --detach "$ZMK_REV"
        update=1
    fi
    # Also covers an init whose update never finished.
    [[ -d "$KEYCHRON_WS/zephyr" ]] || update=1
    if [[ "$update" -eq 1 ]]; then
        local cache_args=()
        [[ -d "$PATH_CACHE/.west" ]] && cache_args+=(--path-cache "$PATH_CACHE")
        (cd "$KEYCHRON_WS" && west update --narrow "${cache_args[@]}")
        # Each cmake cache was configured against the previous revision.
        FRESH=1
    fi
}

read_targets() {
    python3 - "$REPO_DIR/keychron-config/build.yaml" "$TARGET" << 'PYEOF'
import sys, yaml, json

build_yaml = sys.argv[1]
filter_name = sys.argv[2] if len(sys.argv) > 2 else ""

with open(build_yaml) as f:
    data = yaml.safe_load(f)

results = []
for t in data.get("include", []):
    board  = t.get("board", "")
    shield = t.get("shield", "")
    name   = t.get("artifact-name", board)
    cmake  = t.get("cmake-args", "")
    if filter_name and name != filter_name:
        continue
    results.append({"name": name, "board": board, "shield": shield, "cmake_args": cmake})

if filter_name and not results:
    print(f"ERROR: artifact-name '{filter_name}' not found in keychron-config/build.yaml", file=sys.stderr)
    sys.exit(1)

print(json.dumps(results))
PYEOF
}

build_target() {
    local name="$1" board="$2" shield="$3" cmake_args="$4"
    local build_dir="$BUILD_ROOT/$name"

    echo ""
    echo "========================================="
    echo "  Building: $name"
    echo "  Board:    $board"
    [[ -n "$shield" ]] && echo "  Shield:   $shield"
    echo "  ZMK:      $ZMK_REV"
    echo "  SDK:      $ZEPHYR_SDK_INSTALL_DIR"
    echo "========================================="

    if [[ "$FRESH" -eq 1 || ! -d "$build_dir" ]]; then
        echo "→ Configuring (fresh)..."
        rm -rf "$build_dir"
        local shield_args=()
        [[ -n "$shield" ]] && shield_args+=(-DSHIELD="$shield")
        # ZMK_EXTRA_MODULES=$REPO_DIR as in CI: the repo root is the module that
        # carries boards/keychron/keychron_b1_pro.
        cmake -B "$build_dir" -S "$KEYCHRON_WS/zmk/app" \
            -GNinja \
            -DZephyr_DIR="$KEYCHRON_WS/zephyr/share/zephyr-package/cmake" \
            -DBOARD="$board" \
            "${shield_args[@]}" \
            -DZMK_CONFIG="$ZMK_CONFIG" \
            -DZMK_EXTRA_MODULES="$REPO_DIR" \
            $cmake_args
    else
        echo "→ Incremental build (use --fresh to reconfigure)..."
    fi

    # Same reason as local_build_roba_kesb.sh: never ship last run's image.
    rm -f "$build_dir/zephyr/zmk.uf2" "$build_dir/zephyr/zmk.hex"

    if ! cmake --build "$build_dir"; then
        echo "✗ Build failed: $name"
        return 1
    fi

    local uf2="$build_dir/zephyr/zmk.uf2"
    if [[ -f "$uf2" ]]; then
        cp "$uf2" "$OUTPUT_DIR/$name.uf2"
        echo "✓ $OUTPUT_DIR/$name.uf2"
        if [[ -d "$DOWNLOADS" ]]; then cp "$uf2" "$DOWNLOADS/$name.uf2" && echo "✓ $DOWNLOADS/$name.uf2"; fi
    else
        echo "✗ No zmk.uf2 for $name"
        return 1
    fi
}

sync_workspace

export ZEPHYR_BASE="$KEYCHRON_WS/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR

mkdir -p "$OUTPUT_DIR"

targets_json="$(read_targets)"
failed=()

# One JSON object per line, as in local_build_roba_kesb.sh: a tab-separated
# read would collapse the empty shield field and shift the rest left.
while IFS= read -r entry; do
    field() { echo "$entry" | python3 -c "import sys,json; print(json.load(sys.stdin)['$1'])"; }
    if ! build_target "$(field name)" "$(field board)" "$(field shield)" "$(field cmake_args)"; then
        failed+=("$(field name)")
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
    echo "All Keychron builds succeeded."
fi
