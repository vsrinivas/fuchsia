# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import Iterator
from difl.changes import ClassifiedChange


def text_output(abi_changes: Iterator[ClassifiedChange]):
    hardness = {True: 'HARD', False: 'SOFT'}

    for c in abi_changes:
        print('{}: {}'.format(hardness[c.hard], c.message))