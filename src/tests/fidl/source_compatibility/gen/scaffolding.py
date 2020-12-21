# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Functions responsible for actually writing out source files, driven by the
generate_test.py code.
"""

import datetime
import os
from pathlib import Path

from types_ import (HLCPP, LLCPP, RUST, DART, GO)

year = datetime.datetime.now().year

fuchsia_copyright = '''
// Copyright {year} The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
'''.format(year=year).strip()

fidl_file = '''
{lib_decl};

// [START contents]
// INSERT FIDL HERE
// [END contents]
'''

hlcpp_init = '''

#include <fidl/test/{library_name}/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::{library_name};

// [START contents]
// INSERT TEST CODE HERE
// [END contents]

int main(int argc, const char** argv) {{ return 0; }}
'''

llcpp_init = '''

#include <fidl/test/{library_name}/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::{library_name};

// [START contents]
// INSERT TEST CODE HERE
// [END contents]

int main(int argc, const char** argv) {{ return 0; }}
'''

rust_init = '''

#![allow(dead_code)]

use fidl_fidl_test_{library_name} as fidl_lib;

// [START contents]
// INSERT TEST CODE HERE
// [END contents]

fn main() {{}}
'''

go_init = '''

package main

import (
    lib "fidl/fidl/test/{library_name}"
    "syscall/zx/fidl"
)

// [START contents]
// INSERT TEST CODE HERE
// [END contents]

func main() {{}}
'''

dart_init = '''

import 'package:fidl_fidl_test_{library_name}/fidl_async.dart' as fidllib;

// [START contents]
// INSERT TEST CODE HERE
// [END contents]
'''

init_by_binding = {
    HLCPP: hlcpp_init,
    LLCPP: llcpp_init,
    RUST: rust_init,
    GO: go_init,
    DART: dart_init,
}

gn_template = f'# Copyright {year}' + ''' The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import("//src/tests/fidl/source_compatibility/fidl_source_compatibility.gni")

source_compatibility_test("{library_name}") {{
  json = "test.json"
  sidecar = "test_gn_sidecar.json"
}}

group("tests") {{
  deps = [ ":{library_name}" ]
}}
'''


def initialize_fidl(path: Path, library_name: str):
    os.makedirs(path.parent, exist_ok=True)
    initialize_file(path, get_fidl(library_name))


def initialize_src(path: Path, binding: str, fidl_library_name: str):
    os.makedirs(path.parent, exist_ok=True)
    initialize_file(path, get_src(binding, fidl_library_name))


def initialize_file(path: Path, contents: str):
    if path.exists():
        return
    with open(path, 'w+') as f:
        f.write(contents)
    block_on_prompt(
        f'Add starter code to {path.name} as desired, then press enter to continue '
    )


def add_file(src_dir, prev: str, curr: str):
    if (src_dir / curr).exists():
        return
    with open(src_dir / prev, 'r') as previous_f:
        with open(src_dir / curr, 'w+') as current_f:
            contents = previous_f.read()
            current_f.write(contents)

    block_on_prompt(f'Modify {curr} as desired, then press enter to continue ')


def get_fidl(library_name: str) -> str:
    return fuchsia_copyright + fidl_file.format(
        lib_decl=fidl_lib_decl(library_name))


def fidl_lib_decl(library_name: str) -> str:
    return f'library fidl.test.{library_name}'


def get_src(binding: str, library_name: str) -> str:
    return fuchsia_copyright + init_by_binding[binding].format(
        library_name=library_name)


def block_on_prompt(prompt: str):
    """ Prints the prompt, and blocks on user input, then clears the prompt. """
    input(prompt)
    # clear the prompt
    move_cursor_up_n(2)
    print('\r' + ' ' * len(prompt), end='\r')


def move_cursor_up_n(n: int):
    print(f'\033[{n}A')
