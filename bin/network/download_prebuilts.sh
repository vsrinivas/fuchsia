#!/bin/sh

./third_party/mojo/src/mojo/public/tools/download_shell_binary.py \
	--tools-directory=../../../../lesnet/tools \
	--version-file=../../../../lesnet/third_party/mojo/MOJO_VERSION

./third_party/mojo/src/mojo/public/tools/download_dart_snapshotter.py \
	--tools-directory=../../../../lesnet/tools \
	--version-file=../../../../lesnet/third_party/mojo/MOJO_VERSION

# TODO(smklein): Remove this hack. "asio" does not exist in Mojo's third_party
# directory, so this is necessary to build.
cd ./third_party/
git clone https://mojo.googlesource.com/asio
cd ./asio/
git checkout mojo
cd ../..
