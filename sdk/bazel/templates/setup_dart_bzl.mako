# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(":pub_repository.bzl", "pub_repository")

def setup_dart():
    % for name, version in data.iteritems():
    pub_repository(
        name = "vendor_${name}",
        output = ".",
        package = "${name}",
        version = "${version}",
        pub_deps = [],
    )
    % endfor
