# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from enum import Enum
from dataclasses import dataclass
import json
import os
from pathlib import Path
import sys
from typing import Optional

from generate_docs import write_docs
from generate_gn import write_gn
from generate_sidecar import write_gn_sidecar
import scaffolding
from types_ import *
from util import test_name_to_fidl_name, pink, print_err, print_warning, white, TEST_FILE

FIDL_DIR = 'fidl'
EXTENSIONS = {
    HLCPP: 'cc',
    LLCPP: 'cc',
    RUST: 'rs',
    DART: 'dart',
    GO: 'go',
}


class StepKind:
    FIDL = 1
    BINDING = 2


@dataclass
class TransitionState:
    """
    The internal state of the tool, which keeps track of the current point in
    the transition. The test field is the source of truth, and the other fields
    can be derived from the test but are stored for convenience.

    Fields:
        test: The compatibility test so far.
        step_kind: Whether the next step is a FIDL change, or a bindings change.
                   It can be None if the user has not yet defined what kind the
                   first step will be.
        step_num: Number of the current step. The steps are numbered from 1.
        prev_fidl: Name of the most recent FIDL file relative to the FIDL dir.
                   This is used to know which file to copy when creating the next
                   FIDL state.
    """
    test: CompatTest
    step_kind: Optional[StepKind]
    step_num: int
    prev_fidl: str
    prev_srcs: Dict[str, RelativePath]

    @classmethod
    def from_test(cls, test: CompatTest):
        """
        Reconstructs the state of the tool based on an existing test. This is
        used to e.g. resume running the tool on an existing test.
        """
        max_step = -1
        prev_fidl = None
        prev_srcs = {}
        step_kind = None
        for binding, transition in test.bindings.items():
            prev_fidl = prev_fidl or transition.starting_fidl
            prev_srcs[binding] = transition.starting_src
            for step in transition.steps:
                if isinstance(step, SourceStep):
                    prev_srcs[binding] = step.source
                    if step.step_num > max_step:
                        step_kind = StepKind.FIDL
                elif isinstance(step, FidlStep) and step.step_num > max_step:
                    step_kind = StepKind.BINDING
                    prev_fidl = step.fidl
                max_step = max(max_step, step.step_num)

        return TransitionState(
            test=test,
            step_kind=step_kind,
            step_num=max_step + 1,
            prev_fidl=f'{prev_fidl}.test.fidl',
            prev_srcs=prev_srcs)


# A State of None represents a test that hasn't been initialized yet.
State = Optional[TransitionState]


def run(test_root: Path, state: State):
    """ Runs the tool until the user quits. """
    print_warning(
        'Generate test tool: press ^C at any time to quit (progress is saved after each step)'
    )
    while True:
        state = step(test_root, state)
        maybe_save_state(test_root, state)


def step(test_root: Path, state: State) -> State:
    """
    Runs a set of prompts for the user to define a single step in the
    transition. A step is defined as either a change to the bindings, or a
    change to the FIDL library (or setting up the initial state of the test).
    """
    # New test, run setup
    if state is None:
        print(white('Step 0: Define initial FIDL and source states'))
        (new_test, starting_fidl) = run_test_setup(test_root)
        return TransitionState(
            test=new_test,
            step_kind=None,
            step_num=0,
            prev_fidl=starting_fidl,
            prev_srcs={b: s.starting_src for b, s in new_test.bindings.items()})
    # Initial states have been defined, but we don't know if the first step
    # should be a FIDL change or a bindings change.
    elif state.step_kind is None:
        print(
            white(
                f'Step 0.5: Define whether the first step is a FIDL change or binding change'
            ))
        return TransitionState(
            test=state.test,
            step_kind=read_step_kind(),
            step_num=1,
            prev_fidl=state.prev_fidl,
            prev_srcs=state.prev_srcs)
    # Change the FIDL library
    elif state.step_kind == StepKind.FIDL:
        print(white(f'Step {state.step_num}: Define next FIDL library state'))
        new_test = state.test
        fidl_name = input(
            f'Enter name for next {pink("FIDL")} file (e.g. "during.test.fidl"): '
        )
        fidl_name = prepend_step(fidl_name, state.step_num)
        scaffolding.add_file(test_root / FIDL_DIR, state.prev_fidl, fidl_name)

        # for FIDL changes, we add a FidlDef entry, and then reference it in every
        # binding's sequence of steps.
        new_fidl = state.test.fidl
        fidl_ref: FidlRef = stem(fidl_name)
        new_fidl[fidl_ref] = FidlDef(
            source=f'{FIDL_DIR}/{fidl_name}', instructions=read_instructions())
        for binding in new_test.bindings:
            new_test.bindings[binding].steps.append(
                FidlStep(step_num=state.step_num, fidl=fidl_ref))

        return TransitionState(
            test=new_test,
            step_kind=StepKind.BINDING,
            step_num=state.step_num + 1,
            prev_fidl=fidl_name,
            prev_srcs=state.prev_srcs)
    # Change the bindings
    else:
        print(
            white(f'Step {state.step_num}: Define next state for each binding'))
        new_test = state.test
        prev_srcs = {}
        for binding in new_test.bindings:
            filename = input(
                f'Enter name for next {pink(binding)} file (e.g. "during.{EXTENSIONS[binding]}") or leave empty to skip: '
            )
            if not filename:
                continue
            filename = prepend_step(filename, state.step_num)
            path: RelativePath = f'{binding}/{filename}'
            scaffolding.add_file(test_root, state.prev_srcs[binding], path)
            prev_srcs[binding] = path
            # for binding source changes, we append a new SourceStep to the list of steps
            new_test.bindings[binding].steps.append(
                SourceStep(
                    step_num=state.step_num,
                    source=path,
                    instructions=read_instructions()))
        return TransitionState(
            test=new_test,
            step_kind=StepKind.FIDL,
            step_num=state.step_num + 1,
            prev_fidl=state.prev_fidl,
            prev_srcs=prev_srcs)


def run_test_setup(test_root: Path) -> (CompatTest, str):
    """
    Runs the user through prompts to get the minimum amount of information to
    return a new CompatTest.
    """
    title = input(
        'Enter a human readable title for your test (e.g. "Add a protocol method"): '
    )

    # initialize FIDL file
    fidl_name = input(
        f'Enter name for initial {pink("FIDL")} file (e.g. "before.test.fidl"): '
    )
    fidl_name = prepend_step(fidl_name, step=0)
    fidl_library_name = test_name_to_fidl_name(test_root.name)
    scaffolding.initialize_fidl(
        test_root / FIDL_DIR / fidl_name, fidl_library_name)
    fidl_ref: FidlRef = stem(fidl_name)

    # initialize bindings
    bindings = {}
    for binding in BINDINGS:
        filename = input(
            f'Enter name for initial {pink(binding)} file (e.g. "before.{EXTENSIONS[binding]}"), or leave empty to skip binding: '
        )
        if not filename:
            continue
        filename = prepend_step(filename, step=0)
        scaffolding.initialize_src(
            test_root / binding / filename, binding, fidl_library_name)
        bindings[binding] = Steps(
            starting_fidl=fidl_ref,
            starting_src=f'{binding}/{filename}',
            steps=[])
    if not bindings:
        print_err('Must include at least one binding to define a test')
        sys.exit(1)

    new_test = CompatTest(
        title=title,
        fidl={
            fidl_ref: FidlDef(
                source=f'{FIDL_DIR}/{fidl_name}', instructions=[])
        },
        bindings=bindings)

    return (new_test, fidl_name)


def maybe_save_state(test_root: Path, state: State):
    if state.test is not None:
        with open(test_root / TEST_FILE, 'w+') as f:
            json.dump(state.test.todict(), f, indent=4)
        regen_files(test_root, state.test)


def regen_files(test_root: Path, test: CompatTest):
    write_gn_sidecar(test_root, test)
    write_docs(test_root, test)
    write_gn(test_root)


def read_step_kind() -> StepKind:
    prompt = 'Is the first step in the transition a FIDL change (F) or binding change (B)? (F/B) '
    kind = input(prompt)
    while kind not in 'FB':
        print(
            f'Unknown option "{kind}", enter either "F" for FIDL or "B" for binding'
        )
        kind = input(prompt)
    return StepKind.FIDL if kind == 'F' else StepKind.BINDING


def read_instructions() -> List[str]:
    value = input(
        'Enter any instructions for this step, then a blank line to submit. (e.g. "Add [Transitional] attribute"):\n'
    )
    instructions = []
    while value:
        instructions.append(value)
        value = input('')
    return instructions


def stem(p: str) -> str:
    """ Remove all stems from a filename, e.g. foo.test.golden.fidl -> foo. """
    while Path(p).stem != p:
        p = Path(p).stem
    return p


def prepend_step(s: str, step: int) -> str:
    return f'step_{step:02}_{s}'
