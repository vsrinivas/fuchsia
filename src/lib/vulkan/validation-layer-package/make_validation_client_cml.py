# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse

parser = argparse.ArgumentParser()
parser.add_argument("output_file")
parser.add_argument("hash_file")

args = parser.parse_args()

test_hash = open(args.hash_file, "r").read().strip()

open(args.output_file, "w").write(
    """{ children: [
        {
            name: "layer-server",
            url: "fuchsia-pkg://fuchsia.com/validation-layer-package?hash=%s#meta/layer-server.cm",
        },
    ],
    expose: [
        {
            directory: "validation-pkg",
            from: "#layer-server",
        },
    ],
    offer: [
        {
            protocol: [ "fuchsia.logger.LogSink" ],
            from: "parent",
            to: "#layer-server",
        },
    ],
}
    """ % test_hash)
