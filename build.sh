#!/bin/bash
set -exuo pipefail

make qjsc
mkdir -p bin

OS=$(uname -s)
ARCH=$(uname -m)
cp qjsc bin/$OS-$ARCH-qjsc