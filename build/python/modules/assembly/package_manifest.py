# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from dataclasses import dataclass
from typing import Dict, List, Optional
from serialization import serialize_fields_as

__all__ = ['PackageManifest', 'PackageMetaData', 'BlobEntry']

from .common import FilePath


@dataclass(init=False)
@serialize_fields_as(version=str)
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

    def compare_with(
            self,
            other: "BlobEntry",
            allow_source_path_differences=False) -> List[str]:
        """Compare this BlobEntry with the other, reporting any differences.

        If 'allow_source_path_differences' is True, then the source_paths of
        blobs are not compared, just the path, size, and merkle.
        """
        errors = []
        if self.size != other.size:
            errors.append(
                f"blob has a different size ({self.size} vs {other.size}) for: {self.path}"
            )
        elif self.merkle != other.merkle:
            errors.append(
                f"blob has a different merkle ({self.merkle} vs {other.merkle}) for: {self.path}"
            )
        elif not allow_source_path_differences:
            if self.source_path != other.source_path:
                errors.append(
                    f"blob has a different source ({self.source_path} vs {other.source_path}) for: {self.path}"
                )
        return errors


@dataclass
class PackageManifest:
    """The output manifest for a Fuchsia package."""
    package: PackageMetaData
    blobs: List[BlobEntry]
    version: str = "1"

    def blobs_by_path(self) -> Dict[FilePath, BlobEntry]:
        return {blob.path: blob for blob in self.blobs}

    def compare_with(
            self,
            other: "PackageManifest",
            allow_source_path_differences=False) -> List[str]:
        """Compare this package manifest with the other, reporting any
        differences.

        If 'allow_source_path_differences' is True, then the source_paths of
        blobs are not compared, just the path, size, and merkle.
        """
        errors = []
        if self.package.name != other.package.name:
            errors.append(
                f"package publishing names differ: {self.package.name} vs. {other.package.name}"
            )

        self_blobs_by_path = self.blobs_by_path()
        other_blobs_by_path = other.blobs_by_path()

        missing_paths = set(self_blobs_by_path.keys()).difference(
            other_blobs_by_path.keys())
        extra_paths = set(other_blobs_by_path.keys()).difference(
            self_blobs_by_path.keys())
        common_paths = set(self_blobs_by_path.keys()).intersection(
            other_blobs_by_path.keys())

        for missing_path in missing_paths:
            errors.append(f"missing a blob for path: {missing_path}")

        for extra_path in extra_paths:
            errors.append(f"extra blob found at path: {extra_path}")

        for common_path in sorted([str(path) for path in common_paths]):
            self_blob = self_blobs_by_path[common_path]
            other_blob = other_blobs_by_path[common_path]

            errors.extend(
                self_blob.compare_with(
                    other_blob, allow_source_path_differences))

        return errors
