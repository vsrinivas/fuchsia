# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Python Types for Assembly Configuration Files

This module contains Python classes for working with files that have the same
schema as `//src/developer/ffx/plugins/assembly`.
"""

import json
from typing import Dict, Set, Type, TypeVar, Union

__all__ = ["ImageAssemblyConfig", "KernelInfo"]

from .common import FileEntry, FilePath
from .utils import *
from .utils import difference_field, intersect_field

ExtendsImageAssemblyConfig = TypeVar(
    'ExtendsImageAssemblyConfig', bound='ImageAssemblyConfig')


class KernelInfo:
    """Information about the kernel"""

    def __init__(self) -> None:
        self.path: Union[FilePath, None] = None
        self.args: Set[str] = set()
        self.clock_backstop: Union[str, None] = None

    @classmethod
    def from_dict(cls, entry: Dict[str, str]) -> 'KernelInfo':
        """Create from a dictionary (parsed JSON)
        """
        result = cls()
        set_named_member_if_present(result, 'path', entry)
        set_named_member_if_present(result, 'args', entry, transform=set)
        set_named_member_if_present(result, 'clock_backstop', entry)
        return result

    def to_dict(self) -> Dict:
        """Dump the object out as a dict."""
        result = {}
        set_if_named_member_not_empty(result, 'path', self)
        set_if_named_member_not_empty(result, 'args', self, sort=True)
        if self.clock_backstop:
            result['clock_backstop'] = self.clock_backstop
        return result

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


class ImageAssemblyConfig:
    """The input configuration for the Image Assembly Operation

    This describes all the packages, bootfs files, kernel args, kernel, etc.
    that are to be combined into a complete set of assembled product images.
    """

    def __init__(self) -> None:
        """Default constructor
        """
        self.base: Set[FilePath] = set()
        self.cache: Set[FilePath] = set()
        self.system: Set[FilePath] = set()
        self.kernel = KernelInfo()
        self.boot_args: Set[str] = set()
        self.bootfs_files: Set[FileEntry] = set()

    @classmethod
    def from_dict(
            cls: Type[ExtendsImageAssemblyConfig],
            dict: Dict) -> ExtendsImageAssemblyConfig:
        """Create an instance of ImageAssemblyConfig from a dict that was parsed
        """
        result = cls()
        set_named_member_if_present(result, "base", dict, transform=set)
        set_named_member_if_present(result, "cache", dict, transform=set)
        set_named_member_if_present(result, "system", dict, transform=set)
        set_named_member_if_present(
            result, "kernel", dict, transform=KernelInfo.from_dict)
        set_named_member_if_present(result, "boot_args", dict, transform=set)
        set_named_member_if_present(
            result,
            "bootfs_files",
            dict,
            transform=lambda items: set(
                [FileEntry.from_dict(item) for item in items]))
        return result

    @classmethod
    def load(cls, fp) -> 'ImageAssemblyConfig':
        """Create an instance of ImageAssemblyConfig by parsing a JSON file
        """
        parsed = json.load(fp)
        return cls.from_dict(parsed)

    def to_dict(self) -> Dict:
        """Convert to a dictionary that can be serialized to JSON
        """
        result = {}
        set_if_not_empty(result, "base", self.base, sort=True)
        set_if_not_empty(result, "cache", self.cache, sort=True)
        set_if_not_empty(result, "system", self.system, sort=True)
        set_if_not_empty(result, "kernel", self.kernel.to_dict())
        set_if_not_empty(result, "boot_args", self.boot_args, sort=True)
        set_if_not_empty(
            result,
            "bootfs_files",
            sorted(self.bootfs_files),
            transform=FileEntry.to_dict)
        return result

    def dump(self, fp) -> None:
        """Serialize to a file (JSON)
        """
        json.dump(self.to_dict(), fp, indent=2)

    def __repr__(self) -> str:
        """Serialize to a JSON string"""
        return json.dumps(self.to_dict(), indent=2)

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
        return result
