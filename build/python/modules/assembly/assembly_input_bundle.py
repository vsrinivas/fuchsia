# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Python types for Assembly Input Bundles.

Assembly Input Bundles are a set of artifacts that need to be delivered to out-
of-tree (OOT) assembly as a unit, but whose contents should be opaque to the
delivery system itself.

"""
import json
import os
from typing import Dict, List, Set, TextIO, Union

from depfile.depfile import FilePath

from .image_assembly_config import ImageAssemblyConfig
from .common import FileEntry
from .utils import set_named_member_if_present

__all__ = ["AssemblyInputBundle", "ConfigDataEntries"]

PackageName = str
ConfigDataEntries = Dict[PackageName, Set[FileEntry]]


class AssemblyInputBundle(ImageAssemblyConfig):
    """AssemblyInputBundle wraps a set of artifacts together for use by out-of-tree assembly, both
    the manifest of the artifacts, and the artifacts themselves.

    The archived artifacts are placed into a nominal layout that is for readability, but the
    JSON manifest itself is the arbiter of what artifacts are in what categories:

    file layout::

        ./
        manifest.json
        package_manifests/
            base/
                <package name>
            cache/
            system/
        blobs/
            <merkle>
        bootfs/
            path/to/file/in/bootfs
        config_data/
            <package name>/
                <file name>
        kernel/
            kernel.zbi

    manifest schema::

        {
            "base": [ "package1", "package2" ],
            "cache": [ "package3", ... ],
            "system": [ "packageN", ... ],
            "bootfs_files": [
                { "destination": "path/in/bootfs", source: "path/in/layout" },
                ...
            ],
            "config_data": {
                "package1": [
                    { "destination": "path/in/data/dir", "source": "path/in/layout" },
                    { "destination": "path/in/data/dir", "source": "path/in/layout" },
                    ...
                ],
                ...
            },
            "kernel": {
                "path": "kernel/kernel.zbi",
                "args": [ "arg1", "arg2", ... ],
                "clock_backstop": "01234"
            }
            "boot_args": [ "arg1", "arg2", ... ],
        }

    All items are optional.  Files for `config_data` should be in the config_data section,
    not in a package called `config_data`.

    The AssemblyInputBundle is an extension of the ImageAssemblyConfig class, adding new categories
    that it supports which aren't in the ImageAssemblyConfig.
    """

    def __init__(self) -> None:
        super().__init__()
        self.config_data: ConfigDataEntries = {}
        self.blobs: Union[None, Set[FilePath]] = None

    @classmethod
    def from_dict(cls, dict: Dict) -> 'AssemblyInputBundle':
        result = super().from_dict(dict)
        set_named_member_if_present(
            result,
            "bootfs_files",
            dict,
            transform=lambda items: set(
                [FileEntry.from_dict(item) for item in items]))
        if "config_data" in dict:
            for (package, entries) in dict["config_data"].items():
                result.config_data[package] = set(
                    [FileEntry.from_dict(entry) for entry in entries])
        return result

    def to_dict(self) -> Dict:
        """Dump the object out as a dict."""
        result = super().to_dict()
        config_data = {}
        for (package, entries) in sorted(self.config_data.items()):
            config_data[package] = [
                entry.to_dict() for entry in sorted(entries)
            ]
        if config_data:
            result['config_data'] = config_data
        return result

    def write_to(self, file) -> None:
        """Write to a file (JSON format)"""
        json.dump(self.to_dict(), file, indent=2)

    def __repr__(self) -> str:
        """Serialize to a JSON string"""
        return json.dumps(self.to_dict(), indent=2)

    def intersection(
            self, other: 'AssemblyInputBundle') -> 'AssemblyInputBundle':
        """Return the intersection of the two 'ImageAssemblyConfiguration's
        """
        result = super().intersection(other)
        config_data: ConfigDataEntries = {}
        for package in self.config_data.keys():
            if package in other.config_data:
                entries = self.config_data[package]
                other_entries = other.config_data[package]
                entries = entries.intersection(other_entries)
                config_data[package] = entries
        if config_data:
            result.config_data = config_data
        return result

    def difference(self, other: 'AssemblyInputBundle') -> 'AssemblyInputBundle':
        """Return the difference of the two 'ImageAssemblyConfiguration's
        """
        result = super().difference(other)
        for (package, entries) in self.config_data.items():
            if package not in other.config_data:
                result.config_data[package] = entries
            else:
                entries = entries.difference(other.config_data[package])
                if len(entries) > 0:
                    result.config_data[package] = entries
        return result

    def all_file_paths(self) -> List[FilePath]:
        """Return a list of all files that are referenced by this AssemblyInputBundle.
        """
        file_paths = []
        file_paths.extend(self.base)
        file_paths.extend(self.cache)
        file_paths.extend(self.system)
        file_paths.extend([entry.source for entry in self.bootfs_files])
        if self.kernel.path is not None:
            file_paths.append(self.kernel.path)
        for entries in self.config_data.values():
            file_paths.extend([entry.source for entry in entries])
        if self.blobs is not None:
            file_paths.extend(self.blobs)
        return file_paths

    def write_fini_manifest(
            self,
            file: TextIO,
            base_dir: FilePath = None,
            rebase: FilePath = None) -> None:
        """Write a fini-style manifest of all files in the AssemblyInputBundle
        to the given |file|.

        fini manifests are in the format::

          destination/path=path/to/source/file

        As all file paths in the AssemblyInputBundle are relative to the root of
        the bundle, `destination/path` is the existing path.  However, the path
        to the source file cannot be known (absolutely) without additional
        information.

        Arguments:
        - file -- The |TextIO| file to write to.
        - base_dir -- The folder to assume that file paths are relative from.
        - rebase -- The folder to make the source paths relative to, if `base_dir` is also provided.
          By default this is the cwd.

        If `base_dir` is given, it's used to construct the path to the source files, if not, the cwd
        is assumed to be the path the files are from.

        If `rebase` is also given, the path to the source files are then made relative to it.
        """
        file_paths = self.all_file_paths()
        if base_dir is not None:
            file_path_entries = [
                FileEntry(
                    path, os.path.relpath(os.path.join(base_dir, path), rebase))
                for path in file_paths
            ]
        else:
            file_path_entries = [FileEntry(path, path) for path in file_paths]

        FileEntry.write_fini_manifest(file_path_entries, file)
