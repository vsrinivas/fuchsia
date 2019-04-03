# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import subprocess

ROOT_PATH = os.environ["FUCHSIA_DIR"]
FX_PATH = os.path.join(ROOT_PATH, "scripts", "fx")

def _walk_up_path(path):
    res = set([path])
    while True:
        path, basename = os.path.split(path)
        if not path:
            break
        res.add(path)
    return res

def _find_cargo_target(path, target_filter=None):
    match_paths = _walk_up_path(path)
    all_targets = subprocess.check_output([FX_PATH, "build", "-t", "targets"])
    for gn_target in all_targets.split("\n"):
        target_parts = gn_target.split(":")
        if len(target_parts) < 2:
            continue
        target_path, gn_target = target_parts[0], target_parts[1]
        if target_path in match_paths and gn_target.endswith("_cargo"):
            gn_target=gn_target[:gn_target.rindex("_")]
            if target_filter and target_filter != gn_target:
                continue
            yield "{path}:{target}".format(
                    path=target_path,
                    target=gn_target,
            )

class GnTarget:
    def __init__(self, gn_target):
        gn_target = gn_target.lstrip("/")
        gn_target_parts = gn_target.split(":", 1)

        if gn_target_parts[0] == ".":
            cwd_rel_path = os.path.relpath(os.path.abspath("."), ROOT_PATH)
            target_filter = None if len(gn_target_parts) == 1 else gn_target_parts[1]
            gn_targets = list(_find_cargo_target(cwd_rel_path, target_filter))
            if not gn_targets:
                print "No cargo targets found at '{}'".format(cwd_rel_path)
                raise ValueError(gn_target)
            elif len(gn_targets) > 1:
                print "Multiple cargo targets found at '{}'".format(cwd_rel_path)
                for gn_target in gn_targets:
                    print "- {}".format(gn_target)
                raise ValueError(gn_target)
            else:
                gn_target, = gn_targets
                gn_target_parts = gn_target.split(":", 1)

        self.gn_target = gn_target
        self.parts = gn_target_parts

    def __str__(self):
        return self.gn_target

    @property
    def path(self):
        return os.path.join(ROOT_PATH, self.parts[0])

    def manifest_path(self, build_dir=None):
        if len(self.parts) == 1:
            # Turn foo/bar into foo/bar/bar
            path = os.path.join(self.gn_target, os.path.basename(self.gn_target))
        else:
            # Turn foo/bar:baz into foo/bar/baz
            path = self.gn_target.replace(":", os.sep)

        return os.path.join(ROOT_PATH, build_dir, "gen", path, "Cargo.toml")

