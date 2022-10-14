# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pathlib import Path
import os
import sys


# This simple script expects the following:
#   argv[1] = file containing a list of paths
#   argv[2] = stamp file name to write (this is unused but is required for the GN action)
#   argv[3] = directory in which to create hardlinks for all non-host paths from the file in argv[1]
#
# It will proceed to hardlink the (non-host) files listed in the file specified with argv[1] into
# the directory specified by argv[3]. This is mainly helpful for variant builds where the unstripped
# binaries do not automatically get copied to $root_build_dir/exe.unstripped like in non-variant
# builds. If argv[3] exists at the time this script is run, nothing is done (which should indicate
# that this is a non-variant build).
def main():
    dest_dir = Path(sys.argv[3])

    # Always write a stamp file to make the action output happy.
    Path(sys.argv[2]).touch()

    # This means the exe.unstripped directory at the root of the build directory already exists,
    # which means we don't need to do anything and can return early.
    if dest_dir.exists():
        return 0

    dest_dir.mkdir()

    # This is the list of files that came from link_output_rspfile.
    with open(sys.argv[1], 'r') as f:
        for file in f:
            # Only copy files that are built for the target.
            if file.startswith('host_'):
                continue

            source_file = Path(file.rstrip())
            dest_file = dest_dir.joinpath(source_file.name)
            os.link(source_file, dest_file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
