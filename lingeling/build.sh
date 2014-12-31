#!/bin/sh
rm -rf binary
mkdir binary
cd code
./configure.sh || exit 1
make lingeling incling || exit 1
install -m 755 -s lingeling ../binary
install -m 755 -s incling ../binary
