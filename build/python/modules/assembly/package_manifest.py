# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from dataclasses import dataclass
from typing import List, Optional

__all__ = ['PackageManifest', 'PackageMetaData', 'BlobEntry']

from .common import FilePath


@dataclass(init=False)
class PackageMetaData:
    """The metadata that describes a package.

    This is the package manifest's metadata section, which includes both
    intrinsic data like abi_revision, and extrinsic data like the name that it's
    published under and repo that it's for publishing to, etc.
    """
    name: str
    version: int = 0

    def __init__(self, name: str, version: Optional[int] = None) -> None:
        self.name = name
        if version is not None:
            self.version = version
        else:
            self.version = 0


@dataclass
class BlobEntry:
    """A blob that's in the package.

    path - The path that the blob has within the package's namespace.
    merkle - The merkle of the blob
    size - The (uncompressed) size of the blob, in bytes.
    source_path - The path to where the blob was found when the package was
                  being created.
    """
    path: FilePath
    merkle: str
    size: Optional[int]
    source_path: Optional[FilePath]


@dataclass
class PackageManifest:
    """The output manifest for a Fuchsia package."""
    package: PackageMetaData
    blobs: List[BlobEntry]
