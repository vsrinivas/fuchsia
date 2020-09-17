# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hashlib
import os
import platform
import re
import sys
import subprocess

ROOT_PATH = os.environ["FUCHSIA_DIR"]
FX_PATH = os.path.join(ROOT_PATH, "scripts", "fx")
FUCHSIA_BUILD_DIR = os.environ["FUCHSIA_BUILD_DIR"]
PREBUILT_DIR = os.path.join(ROOT_PATH, "prebuilt")
PREBUILT_THIRD_PARTY_DIR = os.path.join(PREBUILT_DIR, "third_party")
HOST_PLATFORM = "%s-%s" % (
    platform.system().lower().replace("darwin", "mac"),
    {
        "x86_64": "x64",
        "aarch64": "arm64",
    }[platform.machine()],
)


class GnTarget:

    def __init__(self, gn_target):
        # [\w-] is a valid GN name. We also accept '/' and '.' in paths.
        # For the toolchain suffix, we take the whole label name at once, so we allow ':'.
        match = re.match(
            r"([\w/.-]*)" + r"(:([\w-]+))?" + r"(\(([\w/:-]+)\))?$", gn_target)
        if match is None:
            print(f"Invalid GN label '{gen_target}'")
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
        return '%s:%s%s' % (
            self.label_path, self.label_name, self.toolchain_suffix)

    @property
    def gn_target(self):
        """The canonical GN label of this target, including the leading '//'."""
        return '//%s' % self.ninja_target

    @property
    def toolchain_suffix(self):
        """The GN path suffix for this target's toolchain, if it is not the default."""
        if self.explicit_toolchain is None:
            return ''
        if 'fuchsia' in self.explicit_toolchain:
            # Default toolchain.
            return ''
        return '(%s)' % self.explicit_toolchain

    @property
    def src_path(self):
        """The absolute path to the directory containing this target's BUILD.gn file."""
        return os.path.join(ROOT_PATH, self.label_path)

    def manifest_path(self, build_dir=None):
        """The path to Cargo.toml for this target."""
        if build_dir is None:
            build_dir = FUCHSIA_BUILD_DIR

        hashed_gn_path = hashlib.sha1(str(
            self.ninja_target).encode("utf-8")).hexdigest()
        return os.path.join(build_dir, "cargo", hashed_gn_path, "Cargo.toml")


def get_rust_target_from_file(file):
    """Given a Rust file, return a GN target that references it. Raises ValueError if the file
    cannot be converted to a target."""
    if not file.endswith(".rs"):
        return None, "Not a Rust file."
    # Query ninja to find the output file.
    ninja_query_args = [
        os.path.join(PREBUILT_THIRD_PARTY_DIR, "ninja", HOST_PLATFORM, "ninja"),
        "-C",
        FUCHSIA_BUILD_DIR,
        "-t",
        "query",
        os.path.relpath(file, FUCHSIA_BUILD_DIR),
    ]

    p = subprocess.Popen(
        ninja_query_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = p.communicate()
    if p.returncode:
        print(err)
        raise None

    # Expected Ninja query output is:
    # ../../filename.rs:
    #   outputs:
    #     rust_crates/binary
    lines = out.splitlines()
    if len(lines) < 3:
        print(f"Unexpected Ninja output: {out}")
        return None

    output_files = [
        os.path.join(FUCHSIA_BUILD_DIR, l.strip()) for l in lines[2:]
    ]

    # For each output file in Ninja, check to see if it's produced by a Rust build
    # target. If so, return the base target name.
    for output_file in output_files:
        # Query GN to get the target that produced that output.
        gn_refs_args = [
            os.path.join(PREBUILT_THIRD_PARTY_DIR, "gn", HOST_PLATFORM, "gn"),
            "refs",
            FUCHSIA_BUILD_DIR,
            output_file,
        ]

        p = subprocess.Popen(
            gn_refs_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        if p.returncode:
            print(err)
            raise None

        # Expected GN refs output is:
        # //path/to/target:bin_build
        # //path/to/target:bin_copy
        lines = out.splitlines()
        for line in lines:
            line = line.strip()
            if line.endswith("_build"):
                return GnTarget(line.rstrip("_build"))

    print(f"Unable to find Rust build target for {file}")
    return None
