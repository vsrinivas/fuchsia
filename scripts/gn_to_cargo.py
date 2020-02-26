#!/usr/bin/env python
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import argparse
import hashlib
import shutil
import re
import sys
import json
import datetime

ROOT_PATH = os.path.abspath(__file__ + "/../..")
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml as toml

CARGO_PACKAGE_CONTENTS = """\
# Copyright %(year)s The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# source GN: %(target)s"

[package]
name = "%(package_name)s"
version = "%(version)s"
license = "BSD-3-Clause"
authors = ["rust-fuchsia@fuchsia.com"]
description = "Rust crate for Fuchsia OS"
repository = "https://fuchsia.googlesource.com"
edition = "%(edition)s"

%(bin_or_lib)s
%(is_proc_macro)s
name = "%(crate_name)s"
path = "%(source_root)s"
"""

CARGO_PACKAGE_DEP = """\
[dependencies.%(crate_name)s]
version = "0.0.1"
path = "%(crate_path)s"

"""


def strip_toolchain(target):
    return re.search("[^(]*", target)[0]


def lookup_gn_pkg_name(project, target):
    metadata = project.targets[target]
    return metadata["output_name"]


def rebase_gn_path(root_path, location, directory=False):
    assert location[0:2] == "//"
    # remove the prefix //
    path = location[2:]
    target = os.path.dirname(path) if directory else path
    return os.path.join(root_path, target)


class Project(object):

    def __init__(self, project_json):
        self.targets = project_json["targets"]
        self.build_settings = project_json["build_settings"]
        self.third_party = None
        self.patches = None

    def rust_targets(self):
        for target in self.targets.keys():
            if "crate_root" in self.targets[target]:
                yield target


def write_toml_file(fout, metadata, project, target, lookup):
    root_path = project.build_settings["root_path"]
    rust_crates_path = os.path.join(root_path, "third_party/rust_crates")

    edition = "2018" if "--edition=2018" in metadata["rustflags"] else "2015"

    if metadata["type"] in ["rust_library", "rust_proc_macro",
                            "static_library"]:
        target_type = "[lib]"
    else:
        if "--test" in metadata["rustflags"]:
            target_type = "[[test]]"
        else:
            target_type = "[[bin]]"

    if metadata["type"] == "rust_proc_macro":
        is_proc_macro = "proc-macro = true"
    else:
        is_proc_macro = ""

    crate_type = "rlib"
    package_name = lookup_gn_pkg_name(project, target)

    fout.write(
        CARGO_PACKAGE_CONTENTS % {
            "target": target,
            "package_name": package_name,
            "crate_name": metadata["crate_name"],
            "version": "0.0.1",
            "year": datetime.datetime.now().year,
            "bin_or_lib": target_type,
            "is_proc_macro": is_proc_macro,
            "lib_crate_type": crate_type,
            "edition": edition,
            "source_root": rebase_gn_path(root_path, metadata["crate_root"]),
            "crate_name": metadata["crate_name"],
            "rust_crates_path": rust_crates_path,
        })

    fout.write("[patch.crates-io]\n")
    for patch in project.patches:
        path = project.patches[patch]["path"]
        fout.write(
            "%s = { path = \"%s/%s\" }\n" % (patch, rust_crates_path, path))

    # collect all dependencies
    deps = []
    for dep in metadata["deps"]:
        # this is a rust target built by cargo
        # TODO remove this when all things use GN. temporary hack
        if "third_party/rust_crates:" in dep:
            match = re.search("rust_crates:([\w-]*)", dep)
            third_party_name = str(match.groups()[0])
            dep_data = project.third_party[third_party_name]
            features = None
            if "features" in dep_data["cargo_dependency_toml"]:
                features = dep_data["cargo_dependency_toml"]["features"]
                version = dep_data["cargo_dependency_toml"]["version"]
            else:
                version = dep_data["cargo_dependency_toml"]
            fout.write("[dependencies.\"%s\"]\n" % third_party_name)
            fout.write("version = \"%s\"\n" % version)
            if features:
                fout.write("features = %s\n" % features)
        # this is a in-tree rust target
        elif "crate_name" in project.targets[dep]:
            crate_name = lookup_gn_pkg_name(project, dep)
            output_name = project.targets[dep]["crate_name"]
            dep_dir = rebase_gn_path(
                root_path, project.build_settings["build_dir"] + "cargo/" +
                str(lookup[dep]))
            fout.write(
                CARGO_PACKAGE_DEP % {
                    "crate_path": dep_dir,
                    "crate_name": crate_name,
                })
        elif "-macro" in dep and len(project.targets[dep]["deps"]) == 1:
            # we need to special case proc macros in the fuchsia tree due to their host target redirection
            # we confirm that this target only has a single dependency because it should only be treated as
            # a macro if it's a single redirect.
            dep = project.targets[dep]["deps"][0]
            crate_name = lookup_gn_pkg_name(project, dep)
            output_name = project.targets[dep]["crate_name"]
            dep_dir = rebase_gn_path(
                root_path, project.build_settings["build_dir"] + "cargo/" +
                str(lookup[dep]))
            fout.write(
                CARGO_PACKAGE_DEP % {
                    "crate_path": dep_dir,
                    "crate_name": crate_name,
                })


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("json_path")
    args = parser.parse_args()
    json_path = args.json_path

    project = None
    try:
        with open(json_path, "r") as json_file:
            project = json.loads(json_file.read())
    except IOError:
        print("Failed to generate Cargo.toml files")
        print("No project.json in the root of your out directory!")
        print("Run gn with the --ide=json flag set")
        # returns 0 so that CQ doesn"t fail if this isn"t set properly
        return 0

    project = Project(project)
    root_path = project.build_settings["root_path"]
    build_dir = project.build_settings["build_dir"]

    third_party_host_path = rebase_gn_path(
        root_path,
        build_dir + "host_x64/rust_third_party_crates/deps_data.json")
    third_party_path = rebase_gn_path(
        root_path, build_dir + "rust_third_party_crates/deps_data.json")
    rust_crates_path = os.path.join(root_path, "third_party/rust_crates")

    # this will be removed eventually
    with open(third_party_path, "r") as json_file:
        project.third_party = json.loads(json_file.read())["crates"]
    with open(third_party_host_path, "r") as json_file:
        project.third_party.update(json.loads(json_file.read())["crates"])
    with open(rust_crates_path + "/Cargo.toml", "r") as f:
        project.patches = toml.load(f)["patch"]["crates-io"]

    host_binaries = []
    target_binaries = []

    lookup = {}
    for idx, target in enumerate(project.rust_targets()):
        # hash is the GN target name without the prefixed //
        lookup[target] = hashlib.sha1(target[2:].encode("utf-8")).hexdigest()

    # remove the priorly generated rust crates
    gn_cargo_dir = rebase_gn_path(
        root_path, project.build_settings["build_dir"] + "cargo/")
    shutil.rmtree(gn_cargo_dir, ignore_errors=True)

    for target in project.rust_targets():
        cargo_toml_dir = rebase_gn_path(
            root_path, project.build_settings["build_dir"] + "cargo/" +
            str(lookup[target]))
        try:
            os.makedirs(cargo_toml_dir)
        except OSError:
            print("Failed to create directory for Cargo: %s" % cargo_toml_dir)

        metadata = project.targets[target]
        with open(cargo_toml_dir + "/Cargo.toml", "w") as fout:
            write_toml_file(fout, metadata, project, target, lookup)
    return 0


if __name__ == "__main__":
    sys.exit(main())
