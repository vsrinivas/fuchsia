#!/bin/bash
set -eu

cd $GOPATH/src/fuchsia.googlesource.com/thinfs/lib/ext2fs
echo -n "Building ext2fs... "
go generate
echo "Done"
