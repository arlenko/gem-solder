#!/usr/bin/env bash
set -euo pipefail

IGNORE_DRC=0

if [ -t 1 ] || [ -t 2 ]; then
    CLR_RED=$'\033[0;31m'
    CLR_YELLOW=$'\033[0;33m'
    CLR_GREEN=$'\033[0;32m'
    CLR_MAGENTA=$'\033[0;35m'
    CLR_RESET=$'\033[0m'
else
    CLR_RED=""
    CLR_YELLOW=""
    CLR_GREEN=""
    CLR_MAGENTA=""
    CLR_RESET=""
fi

usage() {
    echo "Usage: $(basename "$0") [--ignore-drc]" >&2
}

for arg in "$@"; do
    case "$arg" in
    --ignore-drc)
        IGNORE_DRC=1
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown option: $arg" >&2
        usage
        exit 1
        ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/Build}"
FIRMWARE_DIR="$SCRIPT_DIR/firmware"
FIRMWARE_TRANSLATIONS_DIR="$FIRMWARE_DIR/Translations"
FIRMWARE_SOURCE="$FIRMWARE_DIR/source"
FIRMWARE_OUTPUT_DIR="$BUILD_DIR/Firmware"

BOOTLOADER_REPO_URL="https://github.com/arlenko/IronOS-dfu"
BOOTLOADER_BRANCH="Gem"
BOOTLOADER_TEMP_DIR="/tmp/ironos-dfu-gem"
BOOTLOADER_VENV="$BOOTLOADER_TEMP_DIR/venv"
BOOTLOADER_OUTPUT_DIR="$BUILD_DIR/Bootloader"

HARDWARE_PCB="$SCRIPT_DIR/hardware/Gem-solder.kicad_pcb"
GERBER_LAYERS="F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,F.Paste,B.Paste,Edge.Cuts"
PCB_OUTPUT_DIR="$BUILD_DIR/PCB"

# Prefer the poetry/venv-hosted python (has bdflib + pyyaml); fall back to system python3
if [ -x "$FIRMWARE_SOURCE/ironos-venv/bin/python" ]; then
    HOST_PYTHON="$FIRMWARE_SOURCE/ironos-venv/bin/python"
else
    HOST_PYTHON="python3"
fi

AVAILABLE_LANGUAGES=()

clean() {
    echo "${CLR_MAGENTA}***** Cleaning Build directory${CLR_RESET}"
    rm -rf "$BUILD_DIR"
}

render_last_lines() {
    local buf=() i printed=0 line t cols
    cols="${COLUMNS:-$(stty size 2>/dev/null | cut -d' ' -f2)}"
    cols="${cols:-80}"
    while IFS= read -r line; do
        [ ${#buf[@]} -ge 20 ] && buf=("${buf[@]:1}")
        t="${line//$'\r'/}"
        t="${t:0:$((cols - 1))}"
        buf+=("$t")
        [ "$printed" -gt 0 ] && printf '\033[%dA' "$printed"
        printf '\r\033[J'
        for i in "${buf[@]}"; do printf '%s\n' "$i"; done
        printed=${#buf[@]}
    done
}

build_firmware() {
    echo "${CLR_MAGENTA}***** Building firmware${CLR_RESET}"

    if [ ! -d "$FIRMWARE_TRANSLATIONS_DIR" ]; then
        echo "    ${CLR_RED}[Error]${CLR_RESET} Translations dir not found: $FIRMWARE_TRANSLATIONS_DIR" >&2
        exit 1
    fi

    shopt -s nullglob
    local translations=("$FIRMWARE_TRANSLATIONS_DIR"/translation_*.json)
    shopt -u nullglob
    if [ ${#translations[@]} -eq 0 ]; then
        echo "    ${CLR_RED}[Error]${CLR_RESET} No translation_*.json found in $FIRMWARE_TRANSLATIONS_DIR" >&2
        exit 1
    fi

    # Get available languages (upper-case codes, e.g. EN, DE, JA_JP)
    for f in "${translations[@]}"; do
        AVAILABLE_LANGUAGES+=("$(basename "$f" | tr '[:lower:]' '[:upper:]' | sed 's/^TRANSLATION_//; s/\.JSON$//')")
    done

    echo "Languages: ${AVAILABLE_LANGUAGES[*]}"

    pushd "$FIRMWARE_SOURCE" >/dev/null
    make clean >/dev/null
    if [ -t 1 ]; then
        make -j"$(nproc)" model=Gem HOST_PYTHON="$HOST_PYTHON" "${AVAILABLE_LANGUAGES[@]/#/firmware-}" 2>&1 | render_last_lines
    else
        make -j"$(nproc)" model=Gem HOST_PYTHON="$HOST_PYTHON" "${AVAILABLE_LANGUAGES[@]/#/firmware-}"
    fi
    popd >/dev/null

    mkdir -p "$FIRMWARE_OUTPUT_DIR"
    for lang in "${AVAILABLE_LANGUAGES[@]}"; do
        cp "$FIRMWARE_SOURCE/Hexfile/Gem_$lang".{hex,bin,dfu} "$FIRMWARE_OUTPUT_DIR"/
    done

    echo "${CLR_GREEN}***** Firmware written to $FIRMWARE_OUTPUT_DIR${CLR_RESET}"
}

build_bootloader() {
    echo "${CLR_MAGENTA}***** Building bootloader${CLR_RESET}"

    if [ ! -d "$BOOTLOADER_TEMP_DIR/.git" ]; then
        git clone --depth 1 --branch "$BOOTLOADER_BRANCH" "$BOOTLOADER_REPO_URL" "$BOOTLOADER_TEMP_DIR"
    else
        git -C "$BOOTLOADER_TEMP_DIR" fetch --depth 1 origin "$BOOTLOADER_BRANCH"
        git -C "$BOOTLOADER_TEMP_DIR" reset --hard "origin/$BOOTLOADER_BRANCH"
    fi

    if [ ! -x "$BOOTLOADER_VENV/bin/python" ]; then
        python3 -m venv "$BOOTLOADER_VENV"
        "$BOOTLOADER_VENV/bin/pip" install intelhex
    fi

    pushd "$BOOTLOADER_TEMP_DIR" >/dev/null
    export PATH="$BOOTLOADER_VENV/bin:$PATH"

    make clean >/dev/null 2>&1
    if [ -t 1 ]; then
        make all build_type=bootloader model=GEM 2>&1 | render_last_lines
    else
        make all build_type=bootloader model=GEM
    fi
    mkdir -p "$BOOTLOADER_OUTPUT_DIR"
    for f in hex bin dfu; do
        cp "build/bootloader.$f" "$BOOTLOADER_OUTPUT_DIR/Gem_bootloader.$f"
    done

    popd >/dev/null

    echo "${CLR_GREEN}***** Bootloader written to $BOOTLOADER_OUTPUT_DIR${CLR_RESET}"
}

build_pcb() {
    echo "${CLR_MAGENTA}***** Exporting PCB fabrication files${CLR_RESET}"

    if [ ! -f "$HARDWARE_PCB" ]; then
        echo "    ${CLR_RED}[Error]${CLR_RESET} PCB file not found: $HARDWARE_PCB" >&2
        exit 1
    fi

    mkdir -p "$PCB_OUTPUT_DIR"

    if drc_report="$(kicad-cli pcb drc --exit-code-violations -o /dev/stdout "$HARDWARE_PCB" 2>/dev/null)"; then
        echo "    DRC: no violations"
    else
        echo "    DRC violations:"
        echo "$drc_report"
        if [ "$IGNORE_DRC" -eq 1 ]; then
            echo "    ${CLR_YELLOW}[Warning]${CLR_RESET} DRC violations ignored (--ignore-drc)"
        else
            echo "    ${CLR_RED}[Error]${CLR_RESET} DRC has violations - build aborted" >&2
            return 1
        fi
    fi

    kicad-cli pcb export gerbers --layers "$GERBER_LAYERS" --check-zones -o "$PCB_OUTPUT_DIR" "$HARDWARE_PCB" >/dev/null
    kicad-cli pcb export drill --format excellon --excellon-units mm --excellon-separate-th \
        --generate-map --map-format gerberx2 \
        --generate-report --report-path "$PCB_OUTPUT_DIR/Gem-solder-drill_report.txt" \
        -o "$PCB_OUTPUT_DIR" "$HARDWARE_PCB" >/dev/null

    echo "${CLR_GREEN}***** PCB files written to $PCB_OUTPUT_DIR${CLR_RESET}"
}

clean
build_firmware
build_bootloader
build_pcb

echo "${CLR_GREEN}***** Done.${CLR_RESET}"