#!/usr/bin/env python3.8
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hashlib
import os
import platform
import re
import subprocess

ROOT_PATH = os.environ["FUCHSIA_DIR"]
FX_PATH = os.path.join(ROOT_PATH, "scripts", "fx")
FUCHSIA_BUILD_DIR = os.environ["FUCHSIA_BUILD_DIR"]
PREBUILT_DIR = os.path.join(ROOT_PATH, "prebuilt")
PREBUILT_THIRD_PARTY_DIR = os.path.join(PREBUILT_DIR, "third_party")
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
            r"([\w/.-]*)" + r"(:([\w.-]+))?" + r"(\(([\w./:-]+)\))?$", gn_target
        )
        if match is None:
            print(f"Invalid GN label '{gn_target}'")
            raise ValueError(gn_target)
        path, name, toolchain = match.group(1, 3, 5)

        if path.startswith("//"):
            path = path[2:]
        else:
            abs_path = os.path.join(os.path.abspath("."), path)
            path = os.path.relpath(abs_path, ROOT_PATH)

        if name is None:
            name = os.path.basename(path)

        self.label_path = path
        self.label_name = name
        self.explicit_toolchain = toolchain

    def __str__(self):
        return self.gn_target

    @property
    def ninja_target(self):
        """The canonical GN label of this target, minus the leading '//'."""
        return self.label_path + ":" + self.label_name + self.toolchain_suffix

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
        return os.path.join(ROOT_PATH, self.label_path)

    def manifest_path(self, build_dir=None):
        """The path to Cargo.toml for this target."""
        if build_dir is None:
            build_dir = FUCHSIA_BUILD_DIR

        hashed_gn_path = hashlib.sha1(self.ninja_target.encode("utf-8")).hexdigest()
        return os.path.join(build_dir, "cargo", hashed_gn_path, "Cargo.toml")


def targets_from_files(files):
    """Given a list of Rust file, return a set of GN targets that reference them."""
    relpaths = [os.path.relpath(f, FUCHSIA_BUILD_DIR) for f in files]
    ninja = os.path.join(PREBUILT_THIRD_PARTY_DIR, "ninja", HOST_PLATFORM, "ninja")
    call_args = [ninja, "-C", FUCHSIA_BUILD_DIR, "-t", "query"]

    def parse_ninja_output(output, pred):
        lines = output.splitlines()
        outputs = set()
        in_outputs = False
        for line in lines:
            if not in_outputs:
                if line.strip().startswith("outputs:"):
                    in_outputs = True
                continue
            if not line.startswith(" "):
                # we've run out of outputs and moved on to the next rule
                in_outputs = False
            elif pred(line):
                outputs.add(line.strip())
        return outputs

    result = subprocess.run(call_args + relpaths, capture_output=True)
    outputs = parse_ninja_output(
        result.stdout.decode("utf-8"),
        lambda l: l.endswith(".rlib") or "exe.unstripped" in os.path.split(l)[:2],
    )
    outputs = [os.path.basename(p) if "exe.unstripped" in p else p for p in outputs]
    result = subprocess.run(call_args + list(outputs), capture_output=True)
    targets = parse_ninja_output(result.stdout.decode("utf-8"), lambda l: ":" in l)

    return [GnTarget("//" + t) for t in targets]
