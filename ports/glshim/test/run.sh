#!/bin/sh
# Compile the renderer natively and run its self-test.
cd "$(dirname "$0")" && clang -O1 -Wall -Wno-unused-function -Wno-unused-variable -o host_test host_test.c && ./host_test
