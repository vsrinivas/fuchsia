#!/bin/bash
set -eu

# Fetch dependencies
cd $GOPATH/src/fuchsia.googlesource.com/thinfs
go get -u github.com/Masterminds/glide
export PATH=$PATH:$GOPATH/bin/
glide install
