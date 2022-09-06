# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate example directory."""

import argparse
import os
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", required=True, help="Output directory")
    parser.add_argument("--stamp", required=True, help="Output stamp file")
    args = parser.parse_args()

    # Create new directory with desired content.
    new_dir = args.output_dir + '.new'
    if os.path.exists(new_dir):
        shutil.rmtree(new_dir)
    os.makedirs(new_dir)

    # Create ${output_dir}/foo.txt
    with open(os.path.join(new_dir, 'foo.txt'), 'w') as f:
        f.write('This is foo.txt!\n')

    # Create ${output_dir}/bar/bar.txt
    os.makedirs(os.path.join(new_dir, 'bar'))
    with open(os.path.join(new_dir, 'bar', 'bar.txt'), 'w') as f:
        f.write('This is bar.txt!\n')

    # Atomically replace args.output_dir with new_dir
    dir_exists = os.path.exists(args.output_dir)
    if dir_exists:
        old_dir = args.output_dir + '.old'
        if os.path.exists(old_dir):
            shutil.rmtree(old_dir)
        os.rename(args.output_dir, old_dir)

    os.rename(new_dir, args.output_dir)

    if dir_exists:
        shutil.rmtree(old_dir)

    # Create empty stsamp file.
    with open(args.stamp, 'w') as f:
        pass

    return 0


if __name__ == '__main__':
    sys.exit(main())
