#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
    os.path.dirname(             # gn
    os.path.abspath(__file__))))
GN = os.path.join(FUCHSIA_ROOT, "buildtools", "gn")

# The layers of the Fuchsia cake
# Note that these must remain ordered by increasing proximity to the silicon.
LAYERS = [
  'topaz',
  'peridot',
  'garnet',
  'zircon',
]

def main():
    parser = argparse.ArgumentParser('check-layer-dependencies',
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--layer',
                        help='[required] Name of the layer to inspect',
                        choices=LAYERS)
    parser.add_argument('--out',
                        help='Build output directory',
                        default='out/debug-x64')
    args = parser.parse_args()
    layer = args.layer
    out = args.out
    if not layer:
        parser.print_help()
        return 1

    layer_index = LAYERS.index(layer)
    create_labels = lambda layers: list(map(lambda l: '//%s' % l, layers))
    upper_layers = create_labels(LAYERS[0:layer_index])
    lower_layers = create_labels(LAYERS[layer_index:])
    public_labels = subprocess.check_output(
            [GN, 'ls', out, '//%s/public/*' % layer]).splitlines()
    is_valid = True

    for label in public_labels:
        deps = subprocess.check_output(
            [GN, 'desc', out, label, 'deps']).splitlines()
        for dep in deps:
            # We should never depend on upper layers.
            for upper_layer in upper_layers:
                if dep.startswith(upper_layer):
                    is_valid = False
                    print('Upper layer violation')
                    print('  Label %s' % label)
                    print('  Dep   %s' % dep)
            # If we depend on the same layer or a layer below, that dependency
            # should be located in its layer's public directory.
            for lower_layer in lower_layers:
                if (dep.startswith(lower_layer)
                        and not dep.startswith('%s/public' % lower_layer)):
                    is_valid = False
                    print('Lower layer violation')
                    print('  Label %s' % label)
                    print('  Dep   %s' % dep)
    return 0 if is_valid else 1


if __name__ == '__main__':
    sys.exit(main())
