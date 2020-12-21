# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Wrapper layer that adds a sidecar to the test.json data with values needed by the
source_compatibility_test GN template. This is a workaround for the fact that:
* scopes cannot by dynamically modified in GN
* exec_script usage is discouraged.

This exists as a separate file to separate out GN specific data from the test
specification (test.json file), since this data is redundant.
"""

import json
from pathlib import Path

from util import TEST_FILE
from types_ import CompatTest

GN_SIDECAR = 'test_gn_sidecar.json'


def write_gn_sidecar(test_root: Path, test: CompatTest):
    test_name = test_root.name.replace('-', '_')
    with open(test_root / GN_SIDECAR, 'w+') as f:
        json.dump(create_sidecar(test, test_name), f, indent=4)


def create_sidecar(test: CompatTest, test_name: str) -> dict:
    fidl_names = list(test.fidl.keys())
    return {
        'fidl_names': fidl_names,
        'fidl_targets': {n: f'{test_name}_{n}' for n in fidl_names},
        'fidl_sources': {n: test.fidl[n].source for n in fidl_names},
    }
