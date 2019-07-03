#!/bin/bash
SRCROOT=`git rev-parse --show-toplevel`
CFG="$SRCROOT/scripts/uncrustify.cfg"
echo "srcroot: $SRCROOT"
pushd "$SRCROOT"
uncrustify -c "$CFG" --replace --no-backup `git ls-tree --name-only -r HEAD | grep -E '(fp|fpi)-.*\.[ch]$' | grep -v nbis | grep -v fpi-byte | grep -v build/`
popd
