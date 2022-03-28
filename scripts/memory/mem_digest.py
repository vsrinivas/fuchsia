#!/usr/bin/env fuchsia-vendored-python
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from digest import Digest
from snapshot import Snapshot


def main(args):
    snapshot = Snapshot.FromJSONFile(
        sys.stdin
    ) if args.snapshot is None else Snapshot.FromJSONFilename(
        args.snapshot)
    digest = Digest.FromJSONFilename(snapshot, args.digest)

    buckets = sorted(digest.buckets.values(), key=lambda b: b.name)
    if (args.output == "csv"):
        for bucket in buckets:
            print("%s, %d" % (bucket.name, bucket.size))
    else:
        total = 0
        for bucket in buckets:
            if bucket.size == 0:
                continue
            print("%s: %.4gM" % (bucket.name, bucket.size / (1024 * 1024)))
            total += bucket.size
        print("\nTotal: %.4gM" % (total / (1024 * 1024)))

        undigested = {v.koid: v for v in digest.buckets["Undigested"].vmos}
        if len(undigested) > 0:
            print("\nUndigested:")
            for process in snapshot.processes.values():
                bytes = 0
                for vmo in process.vmos:
                    if vmo.koid not in undigested:
                        continue
                    bytes += vmo.committed_bytes
                if bytes == 0:
                    continue
                print("%s: %.4gM" % (process.name, bytes / (1024 * 1024)))

def get_arg_parser():
    parser = argparse.ArgumentParser(description='Convert snapshot to digest.')
    parser.add_argument('-s', '--snapshot')
    parser.add_argument('-d', '--digest')
    parser.add_argument('-o', '--output', default='human')
    return parser

if __name__ == '__main__':
    main(get_arg_parser().parse_args())
