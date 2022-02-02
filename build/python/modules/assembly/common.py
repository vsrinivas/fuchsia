# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Python Types that are shared across different parts of the assembly types

"""
from functools import total_ordering
import os
import shutil
from typing import Dict, Iterable, TextIO, Union
from os import PathLike

__all__ = ["FileEntry", "FilePath", "fast_copy"]

FilePath = Union[str, os.PathLike]


# TODO(fxb/89571) Move to python module at //build/python/modules/file_entry
@total_ordering
class FileEntry:
    """FileEntry Class

    This is a source_path=destination_path mapping type
    """

    def __init__(self, dest_path: FilePath, source_path: FilePath) -> None:
        """Constructor from source and destination paths.
        """
        self.destination = dest_path
        self.source = source_path

    @classmethod
    def from_dict(cls, entry: Dict[str, str]) -> 'FileEntry':
        """Create from a dictionary (parsed JSON)
        """
        return cls(dest_path=entry["destination"], source_path=entry["source"])

    def to_dict(self) -> Dict[str, str]:
        """Serialize to a dictionary
        """
        # Note: This order matches the format in other places, for easier diffing
        return {
            "source": str(self.source),
            "destination": str(self.destination),
        }

    def get_destination(self) -> str:
        """Destination accessor method
        """
        return str(self.destination)

    def __hash__(self):
        return hash((self.destination, self.source))

    def __eq__(self, other) -> bool:
        if isinstance(other, self.__class__):
            return self.destination == other.destination and self.source == other.source
        else:
            return False

    def __lt__(self, other) -> bool:
        if not isinstance(other, FileEntry):
            raise ValueError("other is not a FileEntry")
        return (self.destination,
                self.source) < (other.destination, other.source)

    def __repr__(self) -> str:
        result = "FileEntry{{ destination: {}, source: {} }}".format(
            self.destination, self.source)
        return result

    # TODO(fxb/89571) Move to python module at //build/python/modules/fini_manfest
    @staticmethod
    def write_fini_manifest(
            entries: Iterable['FileEntry'], file: TextIO) -> None:
        """Write out the FileEntry instances into a FINI manifest.

        The fini manifest is in the format of::

            dest_path=source_path

        If the `rebase` path is supplied, all destination paths are made relative to it.
        """
        for entry in sorted(entries):
            dst = entry.destination
            src = entry.source
            file.write("{}={}\n".format(dst, src))


def fast_copy(src: FilePath, dst: FilePath, **kwargs) -> None:
    """A wrapper around os and os.path fns to correctly copy a file using a
    hardlink.
    """
    real_src_path = os.path.realpath(src)
    try:
        os.link(real_src_path, dst, **kwargs)
    except OSError:
        shutil.copy2(real_src_path, dst, **kwargs)
