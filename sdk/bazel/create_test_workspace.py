#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    SCRIPT_DIR)))                # bazel

sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'mako')]
from mako.lookup import TemplateLookup
from mako.template import Template
sys.path += [os.path.join(FUCHSIA_ROOT, 'scripts', 'sdk', 'common')]
from files import copy_tree, make_dir


class SdkWorkspaceInfo(object):
    '''Gathers information about an SDK workspace that is necessary to generate
    tests for it.
    '''

    def __init__(self):
        # Map of target to list of header files.
        # Used to verify that including said headers works properly.
        self.headers = {}
        # Whether the workspace has C/C++ content.
        self.with_cc = False
        # Whether the workspace has Dart content.
        self.with_dart = False


def write_file(path, template_name, data):
    '''Writes a file based on a Mako template.'''
    base = os.path.join(SCRIPT_DIR, 'templates')
    lookup = TemplateLookup(directories=[base, os.path.join(base, 'tests')])
    template = lookup.get_template(template_name + '.mako')
    with open(path, 'w') as file:
        file.write(template.render(data=data))


def create_test_workspace(sdk, output, workspace_info):
    # Remove any existing output.
    shutil.rmtree(output, True)

    # Copy the base tests.
    copy_tree(os.path.join(SCRIPT_DIR, 'tests', 'common'), output)
    if workspace_info.with_cc:
        copy_tree(os.path.join(SCRIPT_DIR, 'tests', 'cc'), output)
    if workspace_info.with_dart:
        copy_tree(os.path.join(SCRIPT_DIR, 'tests', 'dart'), output)

    # WORKSPACE file.
    write_file(os.path.join(output, 'WORKSPACE'), 'workspace', {
        'sdk_path': os.path.relpath(sdk, output),
        'with_cc': workspace_info.with_cc,
        'with_dart': workspace_info.with_dart,
    })

    if workspace_info.with_cc:
        # Generate test to verify that headers compile fine.
        headers = workspace_info.headers
        header_base = os.path.join(output, 'headers')
        write_file(make_dir(os.path.join(header_base, 'BUILD')),
                   'headers_build', {
            'deps': list(filter(lambda k: headers[k], headers.keys())),
        })
        write_file(make_dir(os.path.join(header_base, 'headers.cc')),
                   'headers', {
            'headers': headers,
        })

    return True
