#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os


FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # packages
    os.path.abspath(__file__))))


def get_package_imports(package):
    with open(os.path.join(FUCHSIA_ROOT, package), 'r') as package_file:
        data = json.load(package_file)
    return data['imports'] if 'imports' in data else []

def get_product_imports(product):
    with open(os.path.join(FUCHSIA_ROOT, product), 'r') as product_file:
        data = json.load(product_file)
    imports = []
    imports.extend(data['monolith'] if 'monolith' in data else [])
    imports.extend(data['preinstall'] if 'preinstall' in data else [])
    imports.extend(data['available'] if 'available' in data else [])
    return imports