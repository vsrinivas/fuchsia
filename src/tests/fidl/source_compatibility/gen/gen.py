# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" Implements the gen subcommand for generating tests. """

from collections import deque
from pathlib import Path
from typing import List, Dict, Set

import errors
from steps import Step, add_fidl, add_src, initialize_fidl, initialize_src
from transitions import Binding, State, Transition, Type, FIDL_ASSISTED, MIXED
from util import white


def generate_test(
        library_name: str, root: Path, by_binding: Dict[Binding, Transition]):
    """ Generate the source files for a source compatibility test
    library_name: The FIDL library name used for this test's FIDL files. The
                  full library name will be fidl.test.[library_name]
    root: root path where all files should be emitted.
    by_binding: Describes the test: contains the Transition for each Binding.
    """
    steps = weave_steps(library_name, by_binding)
    print(white(f'Generating test to: {root}'))
    for step in steps:
        step.run(root)


# It would be possible to create a general function that can "weave" an arbitrary
# set of transitions, but given that currently the only mixed transitions are between
# FIDL assisted and source assisted transitions, this case is just hardcoded.
def weave_steps(library_name: str, by_binding: Dict[Binding,
                                                    Transition]) -> List[Step]:
    transitions = set(by_binding.values())
    if len(transitions) == 1:
        return single_transition_to_steps(
            library_name, transitions.pop(), list(by_binding.keys()))
    elif transitions == {FIDL_ASSISTED, MIXED}:
        return mixed_transitions_to_steps(
            library_name, [b for b, t in by_binding.items() if t == MIXED],
            [b for b, t in by_binding.items() if t == FIDL_ASSISTED])
    else:
        raise errors.InvalidTransitionSet(transitions)


def single_transition_to_steps(
        library_name: str, transition: Transition,
        bindings: List[Binding]) -> List[Step]:
    """ Convert the specified Transition into a list of Steps for the provided Bindings. """
    steps: List[Step] = [
        initialize_fidl(library_name, transition.starting_fidl)
    ]
    steps.extend(
        [
            initialize_src(library_name, b, transition.starting_src)
            for b in bindings
        ])

    prev_fidl = transition.starting_fidl
    prev_src = transition.starting_src
    for (type_, state) in transition.changes:
        if type_ == Type.FIDL:
            steps.append(add_fidl(prev_fidl, state))
            prev_fidl = state
        else:
            steps.extend([add_src(b, prev_src, state) for b in bindings])
            prev_src = state
    return steps


def mixed_transitions_to_steps(
        library_name: str, src_assisted: List[Binding],
        fidl_assisted: List[Binding]) -> List[Step]:
    """ Create a list of Steps given the list of source/fidl assisted bindings. """
    steps: List[Step] = [initialize_fidl(library_name, State.BEFORE)]
    steps.extend(
        [
            initialize_src(library_name, b, State.BEFORE)
            for b in src_assisted + fidl_assisted
        ])

    steps.extend([add_src(b, State.BEFORE, State.DURING) for b in src_assisted])
    steps.append(add_fidl(State.BEFORE, State.DURING))
    steps.extend([add_src(b, State.BEFORE, State.AFTER) for b in fidl_assisted])
    steps.append(add_fidl(State.DURING, State.AFTER))
    steps.extend([add_src(b, State.DURING, State.AFTER) for b in src_assisted])

    return steps
