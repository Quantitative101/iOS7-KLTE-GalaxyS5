#!/bin/sh
# Benchmark the renderer's fast paths against the reference paths natively (see bench.c).
cd "$(dirname "$0")" && clang -O2 -Wall -Wno-unused-function -Wno-unused-variable -o bench bench.c && ./bench
