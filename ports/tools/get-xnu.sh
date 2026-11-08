#!/bin/bash
# Fetch the base kernel tree and apply the Galaxy S5 changes: ports/xnu-j1xlte = kOtusin/xnu-j1xlte at e8f2dc9
# (XNU 2107 ARMv7 for the Galaxy J1, from the HTC Leo revival XNU) + xnu/s5-xnu-j1xlte.patch.
#   bash ports/tools/get-xnu.sh
set -eu
P=$(cd "$(dirname "$0")/.." && pwd)
R=$(cd "$P/.." && pwd)
BASE=e8f2dc9ae5f4214e8ba1d5ca13b408c34a671966
if [ -e "$P/xnu-j1xlte" ]; then
    echo "$P/xnu-j1xlte already exists; remove it to start over" >&2
    exit 1
fi
git clone https://github.com/kOtusin/xnu-j1xlte.git "$P/xnu-j1xlte"
git -C "$P/xnu-j1xlte" checkout -q "$BASE"
git -C "$P/xnu-j1xlte" apply --whitespace=nowarn "$R/xnu/s5-xnu-j1xlte.patch"
echo "kernel tree ready: $P/xnu-j1xlte (build with: S5=1 bash ports/tools/build-xnu.sh)"
