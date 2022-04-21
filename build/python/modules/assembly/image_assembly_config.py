# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Python Types for Assembly Configuration Files

This module contains Python classes for working with files that have the same
schema as `//src/developer/ffx/plugins/assembly`.
"""

from dataclasses import dataclass, field
from typing import Optional, Set, TypeVar

import serialization

__all__ = ["ImageAssemblyConfig", "KernelInfo"]

from .common import FileEntry, FilePath
from .utils import difference_field, intersect_field

ExtendsImageAssemblyConfig = TypeVar(
    'ExtendsImageAssemblyConfig', bound='ImageAssemblyConfig')


@dataclass
class KernelInfo:
    """Information about the kernel"""
    path: Optional[FilePath] = None
    args: Set[str] = field(default_factory=set)
    clock_backstop: Optional[int] = None

    def intersection(self, other: 'KernelInfo') -> 'KernelInfo':
        """Return the intersection of the two KernelInfo's
        """
        result = KernelInfo()
        intersect_field(self, other, 'path', result)
        intersect_field(self, other, 'clock_backstop', result)
        result.args = self.args.intersection(other.args)
        return result

    def difference(self, other: 'KernelInfo') -> 'KernelInfo':
        """Return the difference of the two KernelInfo's
        """
        result = KernelInfo()
        difference_field(self, other, 'path', result)
        difference_field(self, other, 'clock_backstop', result)
        result.args = self.args.difference(other.args)
        return result


@dataclass
@serialization.serialize_json
class ImageAssemblyConfig:
    """The input configuration for the Image Assembly Operation

    This describes all the packages, bootfs files, kernel args, kernel, etc.
    that are to be combined into a complete set of assembled product images.
    """
    base: Set[FilePath] = field(default_factory=set)
    cache: Set[FilePath] = field(default_factory=set)
    system: Set[FilePath] = field(default_factory=set)
    kernel: KernelInfo = field(default_factory=KernelInfo)
    boot_args: Set[str] = field(default_factory=set)
    bootfs_files: Set[FileEntry] = field(default_factory=set)
    bootfs_packages: Set[FilePath] = field(default_factory=set)

    def __repr__(self) -> str:
        """Serialize to a JSON string"""
        return serialization.json_dumps(self, indent=2)

    def intersection(
            self: ExtendsImageAssemblyConfig,
            other: 'ImageAssemblyConfig') -> ExtendsImageAssemblyConfig:
        """Return the intersection of the two ImageAssemblyConfiguration's
        """
        result = self.__class__()
        result.base = self.base.intersection(other.base)
        result.cache = self.cache.intersection(other.cache)
        result.system = self.system.intersection(other.system)
        result.kernel = self.kernel.intersection(other.kernel)
        result.boot_args = self.boot_args.intersection(other.boot_args)
        result.bootfs_files = self.bootfs_files.intersection(other.bootfs_files)
        result.bootfs_packages = self.bootfs_packages.intersection(
            other.bootfs_packages)
        return result

    def difference(
            self: ExtendsImageAssemblyConfig,
            other: 'ImageAssemblyConfig') -> ExtendsImageAssemblyConfig:
        """Return the difference of the two ImageAssemblyConfiguration's
        """
        result = self.__class__()
        result.base = self.base.difference(other.base)
        result.cache = self.cache.difference(other.cache)
        result.system = self.system.difference(other.system)
        result.kernel = self.kernel.difference(other.kernel)
        result.boot_args = self.boot_args.difference(other.boot_args)
        result.bootfs_files = self.bootfs_files.difference(other.bootfs_files)
        result.bootfs_packages = self.bootfs_packages.difference(
            other.bootfs_packages)
        return result
