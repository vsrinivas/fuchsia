# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The core types used throughout the gen tool."""

import enum
from dataclasses import dataclass
from typing import Dict, Tuple


class State(enum.Enum):
    BEFORE = 'before'
    DURING = 'during'
    AFTER = 'after'


class Binding(enum.Enum):
    HLCPP = 'hlcpp'
    LLCPP = 'llcpp'
    RUST = 'rust'
    GO = 'go'
    DART = 'dart'


class Type(enum.Enum):
    FIDL = 1
    SOURCE = 2


# frozen to make Transitions hashable
@dataclass(frozen=True)
class Transition:
    starting_fidl: State
    starting_src: State
    changes: Tuple[Tuple[Type, State], ...]

    def __str__(self):
        for name, transition in transitions.items():
            if transition == self:
                return name
        return '<custom transition>'


SOURCE_COMPATIBLE = Transition(
    starting_fidl=State.DURING,
    starting_src=State.BEFORE,
    changes=((Type.SOURCE, State.AFTER),))

FIDL_COMPATIBLE = Transition(
    starting_fidl=State.BEFORE,
    starting_src=State.DURING,
    changes=((Type.FIDL, State.AFTER),))

SOURCE_ASSISTED = Transition(
    starting_fidl=State.BEFORE,
    starting_src=State.BEFORE,
    changes=(
        (Type.SOURCE, State.DURING),
        (Type.FIDL, State.AFTER),
        (Type.SOURCE, State.AFTER),
    ))

FIDL_ASSISTED = Transition(
    starting_fidl=State.BEFORE,
    starting_src=State.BEFORE,
    changes=(
        (Type.FIDL, State.DURING),
        (Type.SOURCE, State.AFTER),
        (Type.FIDL, State.AFTER),
    ))

MIXED = Transition(
    starting_fidl=State.BEFORE,
    starting_src=State.BEFORE,
    changes=(
        (Type.SOURCE, State.DURING),
        (Type.FIDL, State.DURING),
        (Type.FIDL, State.AFTER),
        (Type.SOURCE, State.AFTER),
    ))

transitions = {
    'source-compatible': SOURCE_COMPATIBLE,
    'fidl-compatible': FIDL_COMPATIBLE,
    'source-assisted': SOURCE_ASSISTED,
    'fidl-assisted': FIDL_ASSISTED,
    'mixed': MIXED
}
