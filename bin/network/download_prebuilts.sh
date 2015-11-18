#!/bin/sh

./third_party/mojo/src/mojo/public/tools/download_shell_binary.py \
	--tools-directory=../../../../lesnet/tools \
	--version-file=../../../../lesnet/third_party/mojo/MOJO_VERSION

./third_party/mojo/src/mojo/public/tools/download_dart_snapshotter.py \
	--tools-directory=../../../../lesnet/tools \
	--version-file=../../../../lesnet/third_party/mojo/MOJO_VERSION
