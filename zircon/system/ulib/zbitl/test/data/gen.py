#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import os
import subprocess

ZBI_ALIGNMENT = 8
ZBI_TYPE = "IMAGE_ARGS"

TestDataZbi = collections.namedtuple(
    "TestDataZbi",
    [
        # (str): The basename of the file.
        "name",
        # (seq(str)): A list of text payloads, each to be of type |ZBI_TYPE|.
        "payloads",
    ],
)

TEST_DATA_ZBIS = (
    TestDataZbi(name="empty", payloads=[]),
    TestDataZbi(name="one-item", payloads=["hello world"]),
    # The resulting ZBI will be modified below to indeed give it a bad CRC value.
    TestDataZbi(name="bad-crc-item", payloads=["hello world"]),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zbi", help="Path to the zbi host tool", required=True)
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.realpath(__file__))
    zbi_tool = args.zbi
    for zbi in TEST_DATA_ZBIS:
        assert zbi.name
        output = "%s.zbi" % os.path.join(script_dir, zbi.name)
        json_output = "%s.json" % output
        cmd = [zbi_tool, "--output", output, "--json-output", json_output]
        for payload in zbi.payloads:
            cmd.extend(["--type", ZBI_TYPE, "--entry", payload])
        subprocess.run(cmd, check=True, capture_output=True)

        # Fill in the last |ZBI_ALIGNMENT|-many bytes, as that is sure to
        # affect the payload (and so invalidate the CRC).
        if zbi.name == "bad-crc-item":
            size = os.path.getsize(output)
            with open(output, "r+b") as f:
                f.seek(size - 1 - ZBI_ALIGNMENT)
                f.write(b"\xaa" * ZBI_ALIGNMENT)
            # Remove the now-incorrect JSON.
            os.remove(json_output)


if __name__ == "__main__":
    main()
