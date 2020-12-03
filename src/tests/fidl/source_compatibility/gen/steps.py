# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" Helper functions to create new Steps """

from abc import ABC, abstractmethod
from dataclasses import dataclass
import os
from pathlib import Path
from typing import Optional

import scaffolding
from util import to_fidl_filename, to_src_filename, white
from transitions import State, Binding


# Use classes instead of functions to make testing easier (classes can be compared)
class Step(ABC):
    """
    A Step is a single "action" that runs relative to a specified directory.
    For example, a step can consist of writing or copying a file
    """

    @abstractmethod
    def run(self, root: Path):
        pass


def initialize_fidl(library_name: str, state: State) -> Step:
    """ Return a step that initializes a FIDL source file. """
    return InitializeStep(
        path=to_fidl_filename(state),
        contents=scaffolding.get_fidl(library_name),
        kind='fidl',
        state=state.value,
    )


def initialize_src(library_name: str, binding: Binding, state: State) -> Step:
    """ Return a step that initializes a new source file. """
    return InitializeStep(
        path=to_src_filename(binding, state),
        contents=scaffolding.get_src(binding, library_name),
        kind=binding.value,
        state=state.value)


def add_fidl(prev: State, curr: State) -> Step:
    """ Return a step that adds a new FIDL file based on a previous one. """
    return AddStep(
        from_file=to_fidl_filename(prev),
        to_file=to_fidl_filename(curr),
        kind='fidl',
        state=curr.value)


def add_src(binding: Binding, prev: State, curr: State) -> Step:
    """ Return a step that adds a new source file based on a previous one. """
    return AddStep(
        from_file=to_src_filename(binding, prev),
        to_file=to_src_filename(binding, curr),
        kind=binding.value,
        state=curr.value)


@dataclass
class InitializeStep(Step):
    """ Step that writes contents to a new file. """
    path: Path
    contents: str
    kind: str
    state: str

    def run(self, root: Path):
        path = root / self.path
        print(format_step(self.kind, self.state), end='')
        if path.exists():
            print(' ...already exists')
        else:
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            with open(path, 'w+') as f:
                f.write(self.contents)

            print()
            block_on_prompt(
                f'Add starting code to {self.path}, then press enter to continue '
            )
            move_cursor_up_n(3)
            print(format_step(self.kind, self.state) + ' ...done')


@dataclass
class AddStep(Step):
    """ Step that copies content from an existing file into a new file. """
    from_file: str
    to_file: str
    kind: str
    state: str

    def run(self, root: Path):
        prev = root / self.from_file
        current = root / self.to_file
        print(format_step(self.kind, self.state), end='')
        if current.exists():
            print(' ...already exists')
        else:
            with open(prev, 'r') as previous_f:
                with open(current, 'w+') as current_f:
                    contents = previous_f.read()
                    current_f.write(contents)

            print()
            block_on_prompt(
                f'Modify {self.to_file} as desired, then press enter to continue '
            )
            move_cursor_up_n(3)
            print(format_step(self.kind, self.state) + ' ...done')


def format_step(kind: str, state: str) -> str:
    return f'Define {white(kind)} file for {white(state)} state'


def block_on_prompt(prompt: str):
    """ Prints the prompt, and blocks on user input, then clears the prompt. """
    input(prompt)
    # clear the prompt
    move_cursor_up_n(2)
    print('\r' + ' ' * len(prompt))


def move_cursor_up_n(n: int):
    print(f'\033[{n}A')
