# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Python types for Assembly Input Bundles.

Assembly Input Bundles are a set of artifacts that need to be delivered to out-
of-tree (OOT) assembly as a unit, but whose contents should be opaque to the
delivery system itself.

"""
import json
from typing import Dict, List, Set

from .image_assembly_config import ImageAssemblyConfig
from .common import FileEntry
from .utils import set_if_named_member_not_empty, set_named_member_if_present

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

    def to_dict(self) -> Dict:
        """Dump the object out as a dict."""
        result = super().to_dict()
        config_data = {}
        for (package, entries) in self.config_data.items():
            config_data[package] = [
                entry.to_dict() for entry in sorted(entries)
            ]
        if len(config_data) > 0:
            result['config_data'] = config_data
        return result

    def write_to(self, file) -> None:
        """Write to a file (JSON format)"""
        json.dump(self.to_dict(), file, indent=2)
