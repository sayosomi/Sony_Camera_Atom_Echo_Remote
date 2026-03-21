#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SOURCE_DIR="${ROOT_DIR}"
OUTPUT_DIR="${ROOT_DIR}"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/regenerate-audio-headers.sh [--source-dir <dir>] [--output-dir <dir>]

Defaults:
  --source-dir  repository root
  --output-dir  repository root

Required source files:
  pairing.wav
  connecting.wav
  connected.wav

Shutter sources:
  All files matching shutter*.wav are embedded automatically in numeric order.
  If shutter0.wav exists, it is marked as the cue to skip on the first shot after boot.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-dir)
      SOURCE_DIR="${2:-}"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -d "${SOURCE_DIR}" ]]; then
  echo "Missing source directory: ${SOURCE_DIR}" >&2
  exit 1
fi

if [[ ! -d "${OUTPUT_DIR}" ]]; then
  echo "Missing output directory: ${OUTPUT_DIR}" >&2
  exit 1
fi

SOURCE_DIR="$(cd "${SOURCE_DIR}" && pwd)"
OUTPUT_DIR="$(cd "${OUTPUT_DIR}" && pwd)"

XXD_BIN="$(command -v xxd || true)"
if [[ -z "${XXD_BIN}" ]]; then
  echo "xxd not found on PATH" >&2
  exit 1
fi

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "Missing input file: ${path}" >&2
    exit 1
  fi
}

rewrite_header_symbols() {
  local path="$1"
  sed -E -i '' 's/^unsigned char ([A-Za-z0-9_]+)\[\] = \{$/static const uint8_t \1[] PROGMEM = {/' "${path}"
  sed -E -i '' 's/^unsigned int ([A-Za-z0-9_]+)_len = ([0-9]+);$/static const size_t \1Len = \2;/' "${path}"
}

append_asset() {
  local symbol="$1"
  local filename="$2"
  "${XXD_BIN}" -i -n "${symbol}" "${SOURCE_DIR}/${filename}"
  printf '\n'
}

collect_shutter_files() {
  local file
  find "${SOURCE_DIR}" -maxdepth 1 -type f -name 'shutter*.wav' -exec basename {} \; |
    perl -ne 'print if /^shutter\d+\.wav$/' |
    perl -e '
      my @files = <STDIN>;
      chomp @files;
      @files = sort {
        ($a =~ /^shutter(\d+)\.wav$/)[0] <=> ($b =~ /^shutter(\d+)\.wav$/)[0]
          || $a cmp $b
      } @files;
      print "$_\n" for @files;
    '
}

shutter_symbol_for_file() {
  local filename="$1"
  perl -e '
    my $name = shift;
    $name =~ s/\.wav$//;
    $name =~ s/[^A-Za-z0-9_]/_/g;
    $name =~ s/^([a-z])/\U$1/;
    print $name;
  ' "${filename}"
}

generate_header() {
  local output_path="$1"
  shift

  local tmp_file
  tmp_file="$(mktemp "/tmp/atom-echo-audio-header.XXXXXX")"

  {
    printf '#pragma once\n\n#include <Arduino.h>\n\n'
    while [[ $# -gt 0 ]]; do
      append_asset "$1" "$2"
      shift 2
    done
  } > "${tmp_file}"

  rewrite_header_symbols "${tmp_file}"
  mv "${tmp_file}" "${output_path}"
}

generate_shutter_header() {
  local output_path="$1"
  shift

  local tmp_file
  local index=0
  local first_boot_excluded_index=-1
  local filename
  local shutter_files=("$@")
  tmp_file="$(mktemp "/tmp/atom-echo-shutter-header.XXXXXX")"

  {
    printf '#pragma once\n\n#include "CueAsset.h"\n\n'

    for filename in "${shutter_files[@]}"; do
      local symbol
      symbol="$(shutter_symbol_for_file "${filename}")"
      symbol="k${symbol}Wav"

      append_asset "${symbol}" "${filename}"

      if [[ "${filename}" == "shutter0.wav" ]]; then
        first_boot_excluded_index="${index}"
      fi

      index=$((index + 1))
    done

    printf 'static const CueAsset kShutterCueOptions[] = {\n'

    for filename in "${shutter_files[@]}"; do
      local symbol
      symbol="$(shutter_symbol_for_file "${filename}")"
      symbol="k${symbol}Wav"
      printf '  {%s, %sLen},\n' "${symbol}" "${symbol}"
    done

    printf '};\n'
    printf 'static const size_t kShutterCueCount = sizeof(kShutterCueOptions) / sizeof(kShutterCueOptions[0]);\n'
    printf 'static const int16_t kFirstBootExcludedShutterCueIndex = %d;\n' "${first_boot_excluded_index}"
  } > "${tmp_file}"

  rewrite_header_symbols "${tmp_file}"
  mv "${tmp_file}" "${output_path}"
}

require_file "${SOURCE_DIR}/pairing.wav"
require_file "${SOURCE_DIR}/connecting.wav"
require_file "${SOURCE_DIR}/connected.wav"

SHUTTER_FILES=()
while IFS= read -r shutter_file; do
  SHUTTER_FILES+=("${shutter_file}")
done < <(collect_shutter_files)

if [[ ${#SHUTTER_FILES[@]} -eq 0 ]]; then
  echo "Missing input files: shutter*.wav" >&2
  exit 1
fi

generate_header \
  "${OUTPUT_DIR}/AudioAssets.h" \
  kPairingWav pairing.wav \
  kConnectingWav connecting.wav \
  kConnectedWav connected.wav

generate_shutter_header "${OUTPUT_DIR}/ShutterVariants.h" "${SHUTTER_FILES[@]}"

echo "Generated:"
echo "  ${OUTPUT_DIR}/AudioAssets.h"
echo "  ${OUTPUT_DIR}/ShutterVariants.h"
