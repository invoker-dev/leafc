#!/usr/bin/env bash

URL="https://github.com/shader-slang/slang/releases/download/v2026.2.2/slang-2026.2.2-linux-x86_64.zip"
INSTALL_DIR="./bin/slang"

mkdir -p "$INSTALL_DIR"
echo "downloading slang..."
wget -O "$INSTALL_DIR/slang.zip" "$URL"
echo "extracting slang into $INSTALL_DIR"
unzip "$INSTALL_DIR/slang.zip" -d "$INSTALL_DIR"
rm "$INSTALL_DIR/slang.zip"
echo "slang installed"

