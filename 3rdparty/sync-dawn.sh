#!/bin/sh
GCLIENT=$(which gclient)
if [ -z "$GCLIENT" ]; then
    echo "no gclient installed?"
    exit 1
fi
cd dawn
cp scripts/standalone.gclient .gclient
gclient sync
