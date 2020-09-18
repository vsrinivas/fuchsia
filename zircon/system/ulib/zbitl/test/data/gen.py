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
    TestDataZbi(
        name="multiple-small-items",
        payloads=[
            "Four score and seven years ago our fathers brought forth on this continent, a new nation, conceived in Liberty, and dedicated to the proposition that all men are created equal.",
            "Now we are engaged in a great civil war, testing whether that nation, or any nation so conceived and so dedicated, can long endure.",
            "We are met on a great battle-field of that war.",
            "We have come to dedicate a portion of that field, as a final resting place for those who here gave their lives that that nation might live.",
            "It is altogether fitting and proper that we should do this.",
            "But, in a larger sense, we can not dedicate -- we can not consecrate -- we can not hallow -- this ground.",
            "The brave men, living and dead, who struggled here, have consecrated it, far above our poor power to add or detract.",
            "The world will little note, nor long remember what we say here, but it can never forget what they did here.",
            "It is for us the living, rather, to be dedicated here to the unfinished work which they who fought here have thus far so nobly advanced.",
            "It is rather for us to be here dedicated to the great task remaining before us -- that from these honored dead we take increased devotion to that cause for which they gave the last full measure of devotion -- that we here highly resolve that these dead shall not have died in vain -- that this nation, under God, shall have a new birth of freedom -- and that government of the people, by the people, for the people, shall not perish from the earth.",
        ],
    ),
    TestDataZbi(
        name="second-item-on-page-boundary",
        # Container header:    [0x0000, 0x0020)
        # First item header:   [0x0020, 0x0040)
        # First item payload:  [0x0040, 0x1000)
        # Second item header:  [0x1000, 0x1020)
        # Second item payload: [0x1020, 0x1040)
        payloads=["X" * 4032, "Y" * 32],
    ),
    # The resulting ZBI will be modified below to indeed give it a bad CRC value.
    TestDataZbi(name="bad-crc-item", payloads=["hello world"]),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--zbi", help="Path to the zbi host tool", required=True)
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
