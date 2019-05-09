# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//buckets/ci.star", ci_specs="ci_specs")
load("//buckets/try.star", try_specs="try_specs")

fxicfg.set_output_dir("generated")


def main():
    for spec in ci_specs:
        fxicfg.add_output_file(
            path="ci/" + spec.path,
            data=spec.data,
        )

    for spec in try_specs:
        fxicfg.add_output_file(
            path="try/" + spec.path,
            data=spec.data,
        )


main()
