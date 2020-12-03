# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
from pathlib import Path
from typing import Dict, Set

from transitions import Binding, State

RED = '\033[1;31m'
YELLOW = '\033[1;33m'
WHITE = '\033[1;37m'
RESET = '\033[0m'

binding_to_ext = {
    Binding.HLCPP: 'cc',
    Binding.LLCPP: 'cc',
    Binding.RUST: 'rs',
    Binding.GO: 'go',
    Binding.DART: 'dart',
}


def print_err(s):
    print(format_color(s, RED))


def print_warning(s):
    print(format_color(s, YELLOW))


def white(s):
    return format_color(s, WHITE)


def format_color(s, color):
    return f'{color}{s}{RESET}'


def to_fidl_filename(state: State) -> Path:
    """ Return the filename containing the FIDL library at this state. """
    return Path('fidl') / f'{to_fidl_name(state)}.test.fidl'


def to_fidl_name(state: State) -> str:
    """ Return the name of the FIDL library at this state. """
    return state.value


def to_src_filename(binding: Binding, state: State) -> Path:
    return Path(binding.value) / f'{state.value}.{binding_to_ext[binding]}'


def from_src_filename(path: str) -> State:
    # Assumes path is of the form binding/before.ext
    _, filename = path.split('/')
    state, _ = filename.split('.')
    return State(state)
