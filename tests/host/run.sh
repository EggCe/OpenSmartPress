#!/bin/sh
# Tests der hardwareunabhängigen Logik auf dem PC übersetzen und ausführen.
# Aufruf aus dem Sketch-Verzeichnis: sh tests/host/run.sh   (braucht g++, z. B. in WSL)
set -e
cd "$(dirname "$0")/../.."
mkdir -p build
g++ -std=c++17 -O1 -Wall -Wextra -Werror -Isrc \
    tests/host/test_host.cpp src/pruefsumme.cpp \
    -o build/test_host
./build/test_host
