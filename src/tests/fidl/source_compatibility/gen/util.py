# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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
