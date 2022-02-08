# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse

#
# This script generates the component manifest used by the validation-client component.
#
parser = argparse.ArgumentParser()
parser.add_argument("output_file")
parser.add_argument("hash_file")

args = parser.parse_args()

test_hash = open(args.hash_file, "r").read().strip()

open(args.output_file, "w").write(
    """{ children: [
        {
            name: "validation-server",
            url: "fuchsia-pkg://fuchsia.com/validation-server-pkg?hash=%s#meta/validation-server.cm",
        },
    ],
    expose: [
        {
            directory: "validation_server_pkg",
            from: "#validation-server",
        },
    ],
    offer: [
        {
            protocol: [ "fuchsia.logger.LogSink" ],
            from: "parent",
            to: "#validation-server",
        },
    ],
}
    """ % test_hash)
