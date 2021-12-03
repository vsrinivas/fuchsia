# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import base64
import json
import os
import pathlib
import tempfile


def main():
    parser = argparse.ArgumentParser(
        description="Extract pprof data from inspect.json")
    parser.add_argument(
        "--inspect", required=True, help="Path to inspect.json file")
    parser.add_argument(
        "--component",
        required=True,
        help="Component to extract pprof data for (e.g. core/network/netstack)")
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        help=
        "Directory to extract pprof data to; If not specified, a temporary directory will be created"
    )
    args = parser.parse_args()

    if args.output:
        outdir = args.output
    else:
        outdir = tempfile.mkdtemp()

    print("inspect.json @ %s" % args.inspect)
    print("component = %s" % args.component)
    print("output @ %s" % outdir)

    with open(args.inspect, "r") as f:
        inspect = json.loads(f.read())

    for entry in inspect:
        if entry["data_source"] != "Inspect":
            continue
        if entry["moniker"] != args.component:
            continue

        filepath = entry["metadata"]["filename"]
        # We only care about pprof data.
        if not filepath.startswith("pprof"):
            continue

        filepath = os.path.join(outdir, filepath)
        os.makedirs(filepath, exist_ok=True)

        data = entry["payload"]["root"]["pprof"]
        for k, v in data.items():
            # The first 4 characters are used to indicate that the file is a
            # base64 encoded file.
            prefix, v = v[:4], v[4:]
            if prefix != "b64:":
                print("%s at %s is not base64 encoded" % (k, filepath))

            with open(os.path.join(filepath, k), "wb") as f:
                f.write(base64.b64decode(v))


if __name__ == "__main__":
    main()
