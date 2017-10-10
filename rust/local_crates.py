# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file lists the Rust crates hosted in the Fuchsia tree which have been
# published to crates.io. This ensures we use the local version and not the
# vendored version when compiling other crates.

import os

ROOT_PATH = os.path.abspath(__file__ + "/../../..")

RUST_CRATES = {
    # Local crates.
    # These are usually Fuchsia-owned crates with BUILD.gn files, but may
    # occasionally (and temporarily) contain third-party mirrors with changes
    # specific to Fuchsia. GN files for these mirrors will be hosted under
    # //build/secondary.
    "published": {
        "fuchsia-zircon": {
            "version": "0.3.0",
            "path": "garnet/public/rust/crates/fuchsia-zircon",
            "target": "//garnet/public/rust/crates/fuchsia-zircon",
        },
        "fuchsia-zircon-sys": {
            "version": "0.3.0",
            "path": "garnet/public/rust/crates/fuchsia-zircon/fuchsia-zircon-sys",
            "target": "//garnet/public/rust/crates/fuchsia-zircon/fuchsia-zircon-sys",
        },
        # TODO(pylaligand): move mio and tokio-core back to the mirrors group
        # once fuchsia-zircon is published.
        "mio": {
            "version": "0.6.10",
            "path": "third_party/rust-mirrors/mio",
            "target": "//third_party/rust-mirrors/mio",
        },
        "tokio-core": {
            "version": "0.1.10",
            "path": "third_party/rust-mirrors/tokio-core",
            "target": "//third_party/rust-mirrors/tokio-core",
        },
    },
    # Third-party crates whose sources are mirrored.
    # The GN target for these will be automatically generated under
    # //third_party/rust-crates.
    "mirrors": {
    },
}

def get_all_paths(relative=False):
    mapper = lambda (k, v): (v["path"] if relative
                             else os.path.join(ROOT_PATH, v["path"]))
    def extract_paths(crate_data): return map(mapper, crate_data.iteritems())
    return (extract_paths(RUST_CRATES["published"]) +
            extract_paths(RUST_CRATES["mirrors"]))
