#!/bin/bash
set -exo pipefail

CCACHE=${CCACHE:-0}

emsdk install 3.1.51
emsdk activate 3.1.51
if [ "$CCACHE" == "1" ]
then
    emsdk activate ccache-git-emscripten-64bit
    export _EMCC_CCACHE=1
    export CCACHE=0
fi

make -j`nproc` NATIVE=emscripten BACKTRACE=0 TILES=1 TESTS=0 RUNTESTS=0 RELEASE=1 LINTJSON=0 cataclysm-tiles.js
