# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from typing import List

from pathlib import Path

RED = '\033[1;31m'
YELLOW = '\033[1;33m'
PINK = '\033[1;35m'
WHITE = '\033[1;37m'
RESET = '\033[0m'

TEST_FILE = 'test.json'


def test_name_to_fidl_name(test_name: str) -> str:
    return test_name.replace('-', '')


def print_err(s):
    print(format_color(s, RED))


def print_warning(s):
    print(format_color(s, YELLOW))


def pink(s):
    return format_color(s, PINK)


def white(s):
    return format_color(s, WHITE)


def format_color(s, color):
    return f'{color}{s}{RESET}'


def prepend_step(s: str, step: int) -> str:
    """ Return |s| with the step number prepended to it. """
    return f'step_{step:02}_{s}'


def parse_step(s: str) -> (str, int):
    """
    Parse out a step number prefix, returning the rest of the string and the
    step number. parse_step(prepend_step(s, N)) should equal (s, N).
    """
    s = Path(s).name
    prefix_len = len('step_')
    step_num = int(s[prefix_len:prefix_len + 2])
    return s[prefix_len + 3:], step_num


def find_tests(root: Path) -> List[Path]:
    return [
        p for p in Path(root).iterdir()
        if p.is_dir() and (p / TEST_FILE).exists()
    ]
