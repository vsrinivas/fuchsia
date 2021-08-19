#!/usr/bin/env python3.8

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import csv
import json
import pathlib
import subprocess
import sys
from collections import defaultdict


def load_buildstats(gsutil, bid):
    gs_paths = [
        f'gs://fuchsia-artifacts/builds/{bid}/fuchsia-buildstats.json',
        f'gs://fuchsia-artifacts-internal/builds/{bid}/fuchsia-buildstats.json',
    ]

    for p in gs_paths:
        cmd = subprocess.run([gsutil, 'cat', p], capture_output=True)
        if cmd.returncode == 0:
            return json.loads(cmd.stdout)

    raise Exception(f'buildstats.json for {bid} not found, tried: {gs_paths}')


def main():
    parser = argparse.ArgumentParser(
        description=
        'Count and aggregate stats of critical actions through multiple builds specified by build IDs from a file. This script can take a long time to finish because it takes a few seconds to load and process one build.',
    )
    parser.add_argument(
        '--build_ids',
        help='Path to build IDs file, one build ID per line',
        type=pathlib.Path,
        required=True,
    )
    parser.add_argument(
        '--gsutil',
        help=
        'Path to gsutil, see https://cloud.google.com/storage/docs/gsutil_install#install',
        type=pathlib.Path,
        required=True,
    )
    parser.add_argument(
        '--output',
        help='Path to output results to, output will be in CSV format',
        type=pathlib.Path,
        required=True,
    )
    parser.add_argument(
        '-v',
        '--verbose',
        dest='verbose',
        action='store_true',
        help='Print extra information when this script is running',
    )
    parser.set_defaults(verbose=False)
    args = parser.parse_args()

    with open(args.build_ids, 'r') as f:
        bids = [bid.strip() for bid in f.readlines()]

    if not bids:
        raise Exception(f'No build IDs found in {args.build_ids}')

    counts = defaultdict(int)
    drags = defaultdict(list)
    durations = defaultdict(list)

    i, tot = 0, len(bids)

    if args.verbose:
        print(f'{tot} builds to load and process ...')

    for bid in bids:
        i += 1
        if args.verbose:
            print(
                f'[{i}/{tot}] Loading and counting critical actions of build {bid} ...'
            )

        buildstats = load_buildstats(args.gsutil, bid)
        for step in buildstats['CriticalPath']:
            outputs = ','.join(sorted(step['Outputs']))
            counts[outputs] += 1
            drags[outputs].append(step['Drag'])
            durations[outputs].append(step['End'] - step['Start'])

    if args.verbose:
        print(f'Writing output CSV to {args.output} ...')

    with open(args.output, 'w') as f:
        writer = csv.writer(f)
        writer.writerow(
            ['outputs', 'count', 'average_duration_ms', 'average_drag_ms'])

        sorted_counts = sorted(counts.items(), key=lambda c: c[1], reverse=True)
        for outputs, count in sorted_counts:
            avg_drag = sum(drags[outputs]) / len(drags[outputs]) / 1000000
            avg_dur = sum(durations[outputs]) / len(
                durations[outputs]) / 1000000
            writer.writerow([outputs, count, avg_dur, avg_drag])

    if args.verbose:
        print('DONE')


if __name__ == '__main__':
    sys.exit(main())
