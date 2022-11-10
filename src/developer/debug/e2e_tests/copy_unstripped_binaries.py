# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pathlib import Path
import os
from shutil import rmtree
import sys
import time

# Import //build/images/elfinfo.py
sys.path.insert(0, os.path.dirname(__file__) + '/../../../../build/images')
import elfinfo


def try_link(binary: str, build_id_dir: Path):
    info = elfinfo.get_elf_info(binary)
    build_id = info.build_id
    if info.stripped or not build_id or len(build_id) <= 2:
        return
    dest_dir = build_id_dir / build_id[:2]
    dest_dir.mkdir(exist_ok=True)
    dest = dest_dir / (build_id[2:] + '.debug')
    if not dest.exists():  # When two source binaries resolves to the same.
        os.link(binary, dest)


def main():
    assert len(sys.argv) == 5, "Incorrect number of arguments"

    unstripped_binaries_list_file = Path(sys.argv[1])
    build_id_dir = Path(sys.argv[2])
    depfile = Path(sys.argv[3])
    unstripped_libc = sys.argv[4]

    # Always rebuild the build-id directory for garbage collection, and os.link is fast.
    if build_id_dir.exists():
        rmtree(build_id_dir)

    build_id_dir.mkdir(parents=True)

    depfile_content = str(build_id_dir) + ":"
    with unstripped_binaries_list_file.open() as f:
        for line in f:
            binary = line.rstrip('\n')
            if binary.startswith('host_'):
                continue
            depfile_content += " " + binary
            try_link(binary, build_id_dir)

    depfile_content += " " + unstripped_libc
    try_link(unstripped_libc, build_id_dir)

    depfile.write_text(depfile_content + '\n')


if __name__ == '__main__':
    sys.exit(main())
