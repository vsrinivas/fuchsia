# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import Iterator, List, Optional
from difl.changes import ClassifiedChange
from difl.ir import Declaration

import json
import sys


def tricium_output(abi_changes: Iterator[ClassifiedChange]):
    '''
    Generates output to match the Message proto in:
    https://chromium.googlesource.com/infra/infra/+/HEAD/go/src/infra/tricium/api/v1/data.proto#135
    '''

    messages: List[dict] = []

    for change in abi_changes:
        message: dict = {
                'category': 'difl/abi-hard' if change.hard else 'difl/abi-soft',
                'message': change.message,
        }
        declaration: Optional[Declaration] = change.change.after
        if declaration is not None:
            message['path'] = declaration.filename
            message['start_line'] = declaration.line
            message['end_line'] = declaration.line
        messages.append(message)

    json.dump(messages, sys.stdout, indent=True)
