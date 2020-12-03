# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" Contains all of the FIDL/binding starter code. """

import datetime

from transitions import Binding

year = datetime.datetime.now().year

fuchsia_copyright = '''
// Copyright {year} The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
'''.format(year=year).strip()


def get_fidl(library_name: str) -> str:
    return fuchsia_copyright + fidl_file.format(
        lib_decl=fidl_lib_decl(library_name))


def fidl_lib_decl(library_name: str) -> str:
    return f'library fidl.test.{library_name}'


def get_src(binding: Binding, library_name: str) -> str:
    return fuchsia_copyright + init_by_binding[binding].format(
        library_name=library_name)


fidl_file = '''
{lib_decl};

// INSERT FIDL HERE
'''

hlcpp_init = '''

#include <fidl/test/{library_name}/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::{library_name};

// INSERT TEST CODE HERE

int main(int argc, const char** argv) {{
  return 0;
}}
'''

llcpp_init = '''

#include <fidl/test/{library_name}/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::{library_name};

// INSERT TEST CODE HERE

int main(int argc, const char** argv) {{
  return 0;
}}
'''

rust_init = '''

#![allow(dead_code)]

use fidl_fidl_test_{library_name} as fidl_lib;

// INSERT TEST CODE HERE

fn main() {{}}
'''

go_init = '''

package main

import (
  lib "fidl/fidl/test/{library_name}"
  "syscall/zx/fidl"
)

// INSERT TEST CODE HERE

func main() {{}}
'''

dart_init = '''

import 'package:fidl_fidl_test_{library_name}/fidl_async.dart' as fidllib;

// INSERT TEST CODE HERE
'''

init_by_binding = {
    Binding.HLCPP: hlcpp_init,
    Binding.LLCPP: llcpp_init,
    Binding.RUST: rust_init,
    Binding.GO: go_init,
    Binding.DART: dart_init,
}
