#!/usr/bin/env bash

SHADER_SRC_DIR="../src/shaders"
SHADER_BIN_DIR="../build/bin/shaders"

shopt -s nullglob
SHADER_SRC_FILES=($SHADER_SRC_DIR/*)

SLANGC="../bin/slang/bin/slangc"
FLAGS=(
  # "-warnings-as-errors" "all"
  "-target" "spirv"
  "-I" "$SHADER_SRC_DIR"
)

mkdir -p "$SHADER_BIN_DIR"

for shader_src in "${SHADER_SRC_FILES[@]}"
do
  filename="$(basename -- "$shader_src")"
  echo "compiling: $filename"
  "$SLANGC" "${FLAGS[@]}" -o "$SHADER_BIN_DIR/${filename%.*}.spv" "$shader_src"
done

