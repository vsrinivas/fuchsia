#!/usr/bin/env python2.7
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


class FeatureSpec(object):

    def __init__(self, features, default_features):
        self.features = features
        self.default_features = default_features


class Project(object):

    def __init__(self, project_json):
        self.targets = project_json
        self.patches = None
        self.third_party_features = {}

    def rust_targets(self):
        for target in self.targets.keys():
            if "crate_root" in self.targets[target]:
                yield target

    def dereference_group(self, target):
        """Dereference proc macro shims.

        If the target happens to be a group which just redirects you to a
        different target, returns the real target label. Otherwise, returns
        target.
        """
        meta = self.targets[target]
        if meta["type"] == "group":
            if len(meta["deps"]) == 1:
                dep = meta["deps"][0]
                dep_meta = self.targets[dep]
                return dep
        return target

    def expand_source_set(self, target):
        """Returns a list of dependencies if the target is a source_set.

        Returns dependencies as a list of strings if the target is a
        source_set, or None otherwise.
        """
        meta = self.targets[target]
        if meta["type"] == "source_set":
            return meta["deps"]


def write_toml_file(
        fout, metadata, project, target, lookup, root_path, root_build_dir):
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

    features = []
    feature_pat = re.compile(r"--cfg=feature=\"(.*)\"$")
    for flag in metadata["rustflags"]:
        match = feature_pat.match(flag)
        if match:
            features.append(match.group(1))

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

    if features:
        fout.write("\n[features]\n")
        # Filter 'default' feature out to avoid generating a duplicated entry.
        features = filter(lambda x: x != "default", features)
        fout.write("default = %s\n" % json.dumps(features))
        for feature in features:
            fout.write("%s = []\n" % feature)

    fout.write("\n[patch.crates-io]\n")
    for patch in project.patches:
        path = project.patches[patch]["path"]
        fout.write(
            "%s = { path = \"%s/%s\" }\n" % (patch, rust_crates_path, path))
    fout.write("\n")

    # collect all dependencies
    deps = metadata["deps"]
    while deps:
        dep = deps.pop()
        # handle proc macro shims:
        dep = project.dereference_group(dep)

        # If a dependency points to a source set, expand it into a list
        # of its deps, and append them to the deps list. Finally, continue
        # to the next item, since a source set itself is not considered a
        # dependency for our purposes.
        expanded_deps = project.expand_source_set(dep)
        if expanded_deps:
            deps.extend(expanded_deps)
            continue

        # this is a third-party dependency
        # TODO remove this when all things use GN. temporary hack?
        if "third_party/rust_crates:" in dep:
            has_third_party_deps = True
            match = re.search("rust_crates:([\w-]*)", dep)
            crate_name, version = str(match.group(1)).rsplit("-v", 1)
            version = version.replace("_", ".")
            feature_spec = project.third_party_features.get(crate_name)
            fout.write("[dependencies.\"%s\"]\n" % crate_name)
            fout.write("version = \"%s\"\n" % version)
            if feature_spec:
                fout.write(
                    "features = %s\n" % json.dumps(feature_spec.features))
                if feature_spec.default_features is False:
                    fout.write("default-features = false\n")
        # this is a in-tree rust target
        elif "crate_name" in project.targets[dep]:
            crate_name = lookup_gn_pkg_name(project, dep)
            output_name = project.targets[dep]["crate_name"]
            dep_dir = os.path.join(root_build_dir, "cargo", str(lookup[dep]))
            fout.write(
                CARGO_PACKAGE_DEP % {
                    "crate_path": dep_dir,
                    "crate_name": crate_name,
                })


def main():
    # TODO(tmandry): Remove all hardcoded paths and replace with args.
    parser = argparse.ArgumentParser()
    parser.add_argument("--root_build_dir", required=True)
    parser.add_argument("--fuchsia_dir", required=True)
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
        # returns 0 so that CQ doesn't fail if this isn't set properly
        return 0

    project = Project(project)
    root_path = os.path.abspath(args.fuchsia_dir)
    root_build_dir = os.path.abspath(args.root_build_dir)

    rust_crates_path = os.path.join(root_path, "third_party/rust_crates")

    # this will be removed eventually?
    with open(rust_crates_path + "/Cargo.toml", "r") as f:
        cargo_toml = toml.load(f)
    project.patches = cargo_toml["patch"]["crates-io"]

    # Map from crate name to FeatureSpec. We don't include the version because we don't directly
    # depend on more than one version of the same crate.
    def collect_features(deps):
        for dep, info in deps.iteritems():
            if isinstance(info, str) or isinstance(info, unicode):
                continue
            project.third_party_features[dep] = FeatureSpec(
                info.get("features", []), info.get("default-features", True))

    collect_features(cargo_toml["dependencies"])
    for target_info in cargo_toml["target"].itervalues():
        collect_features(target_info.get("dependencies", {}))

    host_binaries = []
    target_binaries = []

    lookup = {}
    for idx, target in enumerate(project.rust_targets()):
        # hash is the GN target name without the prefixed //
        lookup[target] = hashlib.sha1(target[2:].encode("utf-8")).hexdigest()

    # remove the priorly generated rust crates
    gn_cargo_dir = os.path.join(root_build_dir, "cargo")
    shutil.rmtree(gn_cargo_dir, ignore_errors=True)
    os.makedirs(gn_cargo_dir)
    # Write a stamp file with a predictable name so the build system knows the
    # step ran successfully.
    with open(os.path.join(gn_cargo_dir, "generate_cargo.stamp"), "w") as f:
        f.truncate()

    for target in project.rust_targets():
        cargo_toml_dir = os.path.join(gn_cargo_dir, str(lookup[target]))
        try:
            os.makedirs(cargo_toml_dir)
        except OSError:
            print("Failed to create directory for Cargo: %s" % cargo_toml_dir)

        metadata = project.targets[target]
        with open(os.path.join(cargo_toml_dir, "Cargo.toml"), "w") as fout:
            write_toml_file(
                fout, metadata, project, target, lookup, root_path,
                root_build_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
