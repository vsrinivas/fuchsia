#!/bin/bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -ex

cd ..

# Get depot_tools. Mojo devtools needs this.
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$(pwd)/depot_tools:${PATH}"

# Get gsutil. Our deploy script needs this.
rm -f gsutil.tar.gz
wget https://storage.googleapis.com/pub/gsutil.tar.gz
tar xzf gsutil.tar.gz

# Setup boto
cp modular/modular_tools/continuous_build/boto.tmpl boto
sed -i.bak "s#@TRAVIS_DIR@#`pwd`/modular/modular_tools/continuous_build#g" boto
cd modular
