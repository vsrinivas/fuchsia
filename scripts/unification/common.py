#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A collection of utilities used by scripts in this directory.

import os
import re
import subprocess


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    _SCRIPT_DIR))                 # unification
_JIRI = os.path.join(FUCHSIA_ROOT, '.jiri_root', 'bin', 'jiri')
FX = os.path.join(FUCHSIA_ROOT, 'scripts', 'fx')


def run_command(command):
    return subprocess.check_output(command, cwd=FUCHSIA_ROOT)


def is_tree_clean():
    diff = run_command(['git', 'status', '--porcelain'])
    if diff:
        print('Please make sure your tree is clean before running this script')
        print(diff)
        return False
    return True


_PROJECTS = None


def _list_projects():
    data = run_command([_JIRI, 'runp', _JIRI, 'project'])
    result = {}
    current_project = None
    for line in data.splitlines():
        match = re.match('^\* project (.*)$', line)
        if match:
            current_project = match.group(1)
            continue
        match = re.match('^  Path:\s+(.*)$', line)
        if match:
            result[current_project] = os.path.abspath(match.group(1))
            current_project = None
    return result


def is_in_fuchsia_project(file):
    global _PROJECTS
    if not _PROJECTS:
        _PROJECTS = _list_projects()
    file = os.path.abspath(file)
    for project, base in _PROJECTS.iteritems():
        if project == 'fuchsia':
            # This is the root of the checkout, won't get any useful
            # information out of this one.
            continue
        if os.path.commonprefix([file, base]) == base:
            return False
    return True


def fx_format(path):
    run_command([FX, 'format-code', '--files=' + path])
