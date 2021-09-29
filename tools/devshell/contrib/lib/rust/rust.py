#!/usr/bin/env python3.8
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hashlib
import os
from pathlib import Path
import platform
import re

ROOT_PATH = Path(os.environ["FUCHSIA_DIR"])
FX_PATH = ROOT_PATH / "scripts" / "fx"
FUCHSIA_BUILD_DIR = Path(os.environ["FUCHSIA_BUILD_DIR"])
PREBUILT_DIR = ROOT_PATH / "prebuilt"
PREBUILT_THIRD_PARTY_DIR = PREBUILT_DIR / "third_party"
HOST_PLATFORM = (
    platform.system().lower().replace("darwin", "mac")
    + "-"
    + {"x86_64": "x64", "aarch64": "arm64"}[platform.machine()]
)


class GnTarget:
    def __init__(self, gn_target):
        # [\w-] is a valid GN name. We also accept '/' and '.' in paths.
        # For the toolchain suffix, we take the whole label name at once, so we allow ':'.
        match = re.match(
            r"([\w/.-]*)" + r"(:([\w.-]+))?" + r"(\(([\w./:+-]+)\))?$", gn_target
        )
        if match is None:
            print(f"Invalid GN label '{gn_target}'")
            raise ValueError(gn_target)
        path, name, toolchain = match.group(1, 3, 5)

        if path.startswith("//"):
            path = ROOT_PATH / Path(path[2:])
        else:
            path = Path(path).resolve()

        if name is None:
            name = path.name

        self.label_path = path.relative_to(ROOT_PATH)
        self.label_name = name
        self.explicit_toolchain = toolchain

    def __str__(self):
        return self.gn_target

    @property
    def ninja_target(self):
        """The canonical GN label of this target, minus the leading '//'."""
        return str(self.label_path) + ":" + self.label_name + self.toolchain_suffix

    @property
    def gn_target(self):
        """The canonical GN label of this target, including the leading '//'."""
        return "//" + self.ninja_target

    @property
    def toolchain_suffix(self):
        """The GN path suffix for this target's toolchain, if it is not the default."""
        if self.explicit_toolchain is None or "fuchsia" in self.explicit_toolchain:
            return ""
        return "({})".format(self.explicit_toolchain)

    @property
    def src_path(self):
        """The absolute path to the directory containing this target's BUILD.gn file."""
        return ROOT_PATH / self.label_path

    @property
    def gen_dir(self):
        """The absolute path to the directory containing this target's generated files."""
        return FUCHSIA_BUILD_DIR / "gen" / self.label_path

    def manifest_path(self, build_dir=None):
        """The path to Cargo.toml for this target."""
        if build_dir is None:
            build_dir = FUCHSIA_BUILD_DIR

        hashed_gn_path = hashlib.sha1(self.ninja_target.encode("utf-8")).hexdigest()
        return Path(build_dir) / "cargo" / hashed_gn_path / "Cargo.toml"
