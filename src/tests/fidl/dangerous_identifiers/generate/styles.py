# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

__all__ = ['STYLES']

from dataclasses import dataclass
from typing import List, Callable, Tuple

from generate.types import *

# Define ways that identifiers may be rendered
STYLES: List[Style] = []


def style(func):
    STYLES.append(Style(func.__name__, (func,)))


@style
def lower(ident):
    return '_'.join(w.lower() for w in ident)


@style
def upper(ident):
    return '_'.join(w.upper() for w in ident)


@style
def camel(ident):
    return ''.join(w.capitalize() for w in ident)