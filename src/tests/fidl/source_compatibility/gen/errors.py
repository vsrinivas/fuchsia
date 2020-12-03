# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import Set
from transitions import Transition, State


class InvalidTransitionSet(Exception):

    def __init__(self, transitions: Set[Transition]):
        transitions = [str(t) for t in transitions]
        super().__init__(
            f'\nSpecified transitions cannot be combined: {transitions}\n'
            '(Transitions must all be the same or consist only of --mixed and '
            '--fidl-assisted transitions)')
