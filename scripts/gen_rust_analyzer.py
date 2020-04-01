#!/usr/bin/env python2.7
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Converts project.json (an artifact of gn gen --ide=json) into a rust-project.json file
which can be consumed by rust-analyzer (a language server for Rust)

Syntax of rust-project.json is roughly a path, and a list of edges to dependencies which
are marked by their index in the crates list and the name that rustc expects for complilation.

Three mechanisms:

1: Add syroot crates into the rust-project.json. This isn't expressed in the GN build graph so
   it needs to be done manually. This step in finished before the others start.

2: Add crates that were expressed in GN to the rust-project.json. Traverse the build graph depth-first.
   Cfgs that were marked in the rustflags are properly passed to rust-analyzer.

3: Traverse through dependencies of a rust target which are not rust dependencies. Any non-rust dependency
   may contain rust dependencies (ex: a GN group) and these should be added to the dependency edges of
   the initial target.
"""

import argparse
import os
import platform
import json
import re

# list of crates in a Rust sysroot
sysroot_crates = [
    "std", "core", "alloc", "collections", "libc", "panic_unwind", "proc_macro",
    "rustc_unicode", "std_unicode", "test", "alloc_jemalloc", "alloc_system",
    "compiler_builtins", "getopts", "panic_unwind", "panic_abort", "unwind",
    "build_helper", "rustc_asan", "rustc_lsan", "rustc_msan", "rustc_tsan",
    "syntax"
]
sysroot_edition = "2018"

# if compiled with std, these deps are required
# for sysroot crates
std_deps = [
    "alloc",
    "core",
    "panic_abort",
    "unwind",
]


def strip_toolchain(target):
    """ Remove the toolchain from GN Targets"""
    # TODO Should be be removing the toolchain? If we don't rust-analyzer
    # still works but the build graph is noisy. We might have per-toolchain
    # changes to the target which requires us to have that however.
    return re.search("[^(]*", target).group(0)


def extract_cfg_kv(metadata):
    """ Extract any key value configs """
    kv = {}
    if "rustflags" not in metadata:
        return kv
    rustflags = metadata["rustflags"]
    for flag in rustflags:
        match = re.search("--cfg=(.*)=(.*)", flag)
        if match:
            kv[match.group(1)] = match.group(2)
    return kv


def extract_cfg_atoms(metadata):
    """ Extract any single token configs """
    atoms = []
    if "rustflags" not in metadata:
        return atoms
    rustflags = metadata["rustflags"]
    for flag in rustflags:
        match = re.search("--cfg=([^=]*)$", flag)
        if match:
            atoms.append(match.group(1))
    return atoms


def extract_edition(rustflags):
    """ Find the edition from the rustflags field """
    for flag in rustflags:
        match = re.search("--edition=([0-9]*)$", flag)
        if match:
            return match.group(1)


class Project(object):

    def __init__(self, project_json):
        self.targets = project_json['targets']
        self.build_settings = project_json['build_settings']

    def rust_targets(self):
        for target in self.targets.keys():
            if "crate_root" in self.targets[target]:
                yield target

    def rebase_gn_path(self, path):
        assert path[0:2] == "//"
        root_path = self.build_settings['root_path']
        path = path[2:]  # remove prefix //
        return os.path.join(root_path, path)

    def build_dir(self):
        root_path = self.build_settings['root_path']
        build_dir = self.build_settings['build_dir'][2:]
        return os.path.join(root_path, build_dir)

    def prebuilt_rust(self):
        root_path = self.build_settings['root_path']
        host_platform = "%s-%s" % (
            platform.system().lower().replace("darwin", "mac"),
            {
                "x86_64": "x64",
                "aarch64": "arm64",
            }[platform.machine()],
        )
        return os.path.join(
            root_path, "prebuilt/third_party/rust/%s/" % host_platform)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "project",
        help=
        "Path to project.json (usually contained within your GN out directory)")
    parser.add_argument("--output", help="Output path for rust-project.json")
    args = parser.parse_args()

    project = None
    with open(args.project, 'r') as json_file:
        project = json.loads(json_file.read())

    project = Project(project)

    project_json = {"roots": [], "crates": []}

    # Mapping from GN Target without it's toolchain appended to the index in the edge graph.
    # Sysroot crates are mapped by their bare name
    lookup = {}

    # set of targets seen that aren't rust crates
    non_rust_seen = set()

    def add_sysroot_crate(crate_name):
        """ Add as sysroot crate to the lookup table"""
        if crate_name in lookup:
            return
        crate_path = os.path.join(
            project.prebuilt_rust(),
            "lib/rustlib/src/rust/src/lib%s/lib.rs" % crate_name)
        crate = {}
        crate["root_module"] = crate_path
        crate["edition"] = sysroot_edition
        crate["atom_cfgs"] = []
        crate["key_value_cfgs"] = {}
        crate["crate_id"] = len(project_json["crates"])
        crate["deps"] = []
        lookup[crate_name] = len(project_json["crates"])
        project_json["crates"].append(crate)

        if crate_name == "std":
            for dependency in std_deps:
                if dependency not in lookup:
                    add_sysroot_crate(dependency)
                crate['deps'].append(
                    {
                        "crate": lookup[dependency],
                        "name": dependency
                    })

        if crate_name == "alloc":
            dependency = "core"
            if dependency not in lookup:
                add_sysroot_crate(dependency)
            crate['deps'].append(
                {
                    "crate": lookup[dependency],
                    "name": dependency
                })

    def add_transitive_crates(target):
        """ Traverse through GN groups """
        local_crate_lookup = []
        metadata = project.targets[target]
        for dependency in metadata['deps']:
            dependency_metadata = project.targets[dependency]
            if "crate_root" in dependency_metadata:
                if dependency not in lookup:
                    add_crate(dependency)
                local_crate_lookup += [
                    (
                        lookup[strip_toolchain(dependency)],
                        dependency_metadata['crate_name'])
                ]
            else:
                if dependency not in non_rust_seen:
                    non_rust_seen.add(dependency)
                    add_transitive_crates(target)
        return local_crate_lookup

    def add_crate(target):
        """ Adds a crate to the lookup if it hasn't been seen already"""
        # If we've already seen this target from another toolchain,
        # skip it. rust-analyzer doesn't use toolchain info
        if strip_toolchain(target) in lookup:
            return

        crate = {}
        metadata = project.targets[target]
        crate["root_module"] = project.rebase_gn_path(metadata["crate_root"])
        crate["edition"] = extract_edition(metadata["rustflags"])
        crate["atom_cfgs"] = extract_cfg_atoms(metadata)
        crate["key_value_cfgs"] = extract_cfg_kv(metadata)
        crate["crate_id"] = len(project_json['crates'])
        crate["deps"] = []
        # TODO programatically check for std (or core!)
        crate['deps'].append({"crate": lookup["std"], "name": "std"})
        lookup[strip_toolchain(target)] = len(project_json['crates'])
        project_json["crates"].append(crate)

        for dependency in metadata['deps']:
            dependency_metadata = project.targets[dependency]

            # This is a rust target built by GN
            if "crate_name" in dependency_metadata:
                if strip_toolchain(dependency) not in lookup:
                    add_crate(dependency)
                crate['deps'].append(
                    {
                        "crate": lookup[strip_toolchain(dependency)],
                        "name": dependency_metadata['crate_name']
                    })

            # This is not a rust target.
            # We need to traverse to collect rust dependencies that may not propgate through.
            else:
                for transitive_dep in add_transitive_crates(dependency):
                    crate['deps'].append(
                        {
                            "crate": transitive_dep[0],
                            "name": transitive_dep[1]
                        })

    for target in sysroot_crates:
        add_sysroot_crate(target)

    for target in project.rust_targets():
        add_crate(target)

    fout = os.path.join(project.build_dir(), "rust-project.json")
    # overwrite the default path
    if args.output:
        fout = args.output

    with open(fout, 'w') as f:
        json.dump(project_json, f, ensure_ascii=True)


if __name__ == "__main__":
    main()
