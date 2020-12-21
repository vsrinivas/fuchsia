# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
from pathlib import Path

from util import test_name_to_fidl_name
from scaffolding import gn_template
from types_ import CompatTest


def write_gn(test_root: Path):
    with open(test_root / 'BUILD.gn', 'w+') as f:
        f.write(
            gn_template.format(
                library_name=test_name_to_fidl_name(test_root.name)))
