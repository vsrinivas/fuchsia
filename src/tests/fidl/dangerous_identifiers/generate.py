#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import datetime
import os
import platform
import re
import subprocess
import sys

from typing import List

# Where is this script?
DIR = os.path.dirname(os.path.realpath(__file__))

# What platform are we on?
HOST_PLARFORM = "{}-{}".format(
    platform.system().lower().replace("darwin", "mac"),
    {
        "x86_64": "x64",
        "aarch64": "arm64",
    }[platform.machine()],
)

# Where is gn?
GN = os.path.realpath(
    os.path.join(
        DIR, '../../../../prebuilt/third_party/gn', HOST_PLARFORM, 'gn'))
FUCHSIA_DIR = os.environ.get('FUCHSIA_DIR')
FUCHSIA_BUILD_DIR = os.environ.get('FUCHSIA_BUILD_DIR')
if FUCHSIA_BUILD_DIR is None:
    print('Run "fx exec %s".' % sys.argv[0])
    sys.exit(1)
FIDL_FORMAT = os.path.join(FUCHSIA_BUILD_DIR, 'host_x64', 'fidl-format')
RUST_FORMAT = os.path.join(FUCHSIA_DIR, 'prebuilt', 'third_party', 'rust_tools', 'linux-x64', 'bin', 'rustfmt')


# IdentifierDef represents individual parts of an identifier, along with a tag.
# An identifier definition produces an `Identifier` when styled.
#
# For instance, the defintion
#
#     my_super_identifier:8
#
# has three parts [my, super, identifer] and tag 8.
class IdentifierDef:

    def __init__(self, parts: List[str], tag: int, bindings_denylist: str):
        self.parts = parts
        self.tag = tag
        self.bindings_denylist = bindings_denylist


class Identifier:

    def __init__(self, ident: str, tag: int, bindings_denylist: str):
        self.ident = ident
        self.tag = tag
        self.bindings_denylist = bindings_denylist


# Define ways that identifiers may be rendered
STYLES = []


def style(func):
    STYLES.append((func.__name__, func))


@style
def lower(ident):
    return '_'.join(w.lower() for w in ident)


@style
def upper(ident):
    return '_'.join(w.upper() for w in ident)


@style
def camel(ident):
    return ''.join(w.capitalize() for w in ident)


# Define places that identifiers may appear in a FIDL library:
USES = []


def use(func):
    USES.append((func.__name__.replace('_', '.'), func))


@use
def constants(f, idents: List[Identifier]):
    for ident in idents:
        # TODO(fxbug.dev/38124): Enable this case once we've clarified these edge cases
        # and chosen a way to unambiguously reference the root library. Currently,
        # "const uint32 uint32 = 1;" will fail with an includes-cycle fidlc error.
        if ident.ident == "uint32":
            continue
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('const uint32 %s = 1;\n' % ident.ident)


@use
def using(f, idents: List[Identifier]):
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('using %s = vector;\n' % ident.ident)


# TODO(ianloic): Make this test work. It requires N libraries to import for N
# identifiers. That doesn't fit well into the model of this test.
#@use
#def using_as(f, idents):
#  for ident in idents:
#    f.write('using fuchsia.mem as %s;\n' % ident)


@use
def enums(f, idents: List[Identifier]):
    # enums with every dangerous name
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('enum %s { MEMBER = 1; };\n' % ident.ident)

    # enum with every dangerous field name
    f.write('enum DangerousMembers {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  %s = %d;\n' % (ident.ident, ident.tag - 1))
    f.write('};\n')


@use
def struct_types(f, idents: List[Identifier]):
    # structs with every dangerous name
    f.write('using membertype = uint32;\n')
    for ident in idents:
        # TODO(fxbug.dev/8042): Having a declaration with same same name as what is
        # aliased causes a cycle.
        if ident.ident == "uint32":
            continue
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('struct %s { membertype member = 1; };\n' % ident.ident)

    # a struct with every dangerous name as the field type
    f.write('struct DangerousMembers {\n')
    for ident in idents:
        # dangerous field type
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  %s f%d;\n' % (ident.ident, ident.tag - 1))
    f.write('};\n')


@use
def struct_names(f, idents: List[Identifier]):
    # a struct with every dangerous name as the field name
    f.write('struct DangerousMembers {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  uint32 %s;\n' % ident.ident)
    f.write('};\n')


# TODO(fxbug.dev/8081)
# Temporarily disabled due to superlinear compiler time and peak memory usage.
# @use
# def union_names(f, idents):
#   # unions with every dangerous name
#   f.write('using membertype = uint32;\n')
#   for ident in idents:
#     # TODO(fxbug.dev/8042): Having a declaration with same same name as what is
#     # aliased causes a cycle.
#     if ident == "uint32":
#       continue
#     f.write('union %s { membertype member; };\n' % ident)
#
#   # a union with every dangerous name as the field type
#   f.write('union DangerousMembers {\n')
#   for i, ident in enumerate(idents):
#     # dangerous field type
#     f.write('  %s f%d;\n' % (ident, i))
#   f.write('};\n')
#
#
# @use
# def union_types(f, idents):
#   # a union with every dangerous name as the field name
#   f.write('union DangerousMembers {\n')
#   for i, ident in enumerate(idents):
#     f.write('  uint32 %s;\n' % (ident))
#   f.write('};\n')


@use
def table_names(f, idents: List[Identifier]):
    # tables with every dangerous name
    f.write('using membertype = uint32;\n')
    for ident in idents:
        # TODO(fxbug.dev/8042): Having a declaration with same same name as what is
        # aliased causes a cycle.
        if ident.ident == "uint32":
            continue
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('table %s { 1: membertype member; };\n' % ident.ident)
    # a table with every dangerous name as the field type
    f.write('table DangerousMembers {\n')
    for ident in idents:
        # dangerous field type
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  %s: %s f%s;\n' % (ident.tag, ident.ident, ident.tag - 1))
    f.write('};\n')


@use
def table_fields(f, idents: List[Identifier]):
    # a table with every dangerous name as the field name
    f.write('table DangerousMembers {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  %d: uint32 %s;\n' % (ident.tag, ident.ident))
    f.write('};\n')


@use
def protocol_names(f, idents: List[Identifier]):
    # a protocols with every dangerous name
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('protocol %s { JustOneMethod(); };\n' % ident.ident)


@use
def method_names(f, idents: List[Identifier]):
    # a protocol with every dangerous name as a method name
    f.write('protocol DangerousMethods {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  %s();\n' % ident.ident)
    f.write('};\n')


@use
def event_names(f, idents: List[Identifier]):
    # a protocol with every dangerous name as an event name
    f.write('protocol DangerousEvents {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  -> %s();\n' % ident.ident)
    f.write('};\n')


@use
def method_request_arguments(f, idents: List[Identifier]):
    # a protocol with every dangerous name as a request argument
    f.write('using argtype = uint32;\n')
    f.write('protocol DangerousRequestArguments {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  Method%d(argtype %s);\n' % (ident.tag - 1, ident.ident))
    f.write('};\n')


@use
def method_response_arguments(f, idents: List[Identifier]):
    # a protocol with every dangerous name as a response argument
    f.write('using argtype = uint32;\n')
    f.write('protocol DangerousResponseArguments {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write(
            '  Method%d() -> (argtype %s);\n' % (ident.tag - 1, ident.ident))
    f.write('};\n')


@use
def method_event_arguments(f, idents: List[Identifier]):
    # a protocol with every dangerous name as a event argument
    f.write('using argtype = uint32;\n')
    f.write('protocol DangerousResponseArguments {\n')
    for ident in idents:
        if ident.bindings_denylist:
            f.write('[BindingsDenylist="%s"]\n' % ident.bindings_denylist)
        f.write('  -> Event%d(argtype %s);\n' % (ident.tag - 1, ident.ident))
    f.write('};\n')


def generated(prefix):
    """Return a header line indicating that this is a generated file."""
    return """{prefix} Copyright 2019 The Fuchsia Authors. All rights reserved.
{prefix} Use of this source code is governed by a BSD-style license that can be
{prefix} found in the LICENSE file.
{prefix} Generated by {generator}.
""".format(
        prefix=prefix,
        year=datetime.datetime.now().year,
        generator=os.path.basename(__file__))


def library_target(library_name):
    return '//src/tests/fidl/dangerous_identifiers/fidl:%s' % library_name


def dangerous_identifiers() -> List[IdentifierDef]:
    """Load a list of dangerous identifiers definitions from the source tree.

  Verifies that all definitions are well formed, and that their tags are unique.
  """
    file_path = os.path.join(DIR, 'dangerous_identifiers.txt')
    lines = (
        line.strip()
        for line in open(file_path).readlines()
        if not line.startswith('#'))

    failed = False
    idents = []
    tags_seen = set()
    for line in lines:
        line_search = re.match(
            r'^([a-z0-9_]+):([0-9]+)(\s+BindingsDenylist=(.+))?$', line)
        if line_search:
            parts = line_search.group(1).split('_')
            tag = int(line_search.group(2))
            bindings_denylist = ""
            if line_search.group(4):
                bindings_denylist = line_search.group(4)
            if tag not in tags_seen:
                tags_seen.add(tag)
                idents.append(IdentifierDef(parts, tag, bindings_denylist))
            else:
                failed = True
                print("line '%s' has duplicate tag" % line)
        else:
            failed = True
            print("line '%s' is malformed" % line)

    if failed:
        exit(1)
    return idents


def generate_fidl(identifier_defs: List[IdentifierDef]) -> List[str]:
    """Generate FIDL libraries for the specified identifier definitions.

  Return the list of library names.
  """
    directory = os.path.join(DIR, 'fidl')
    os.makedirs(directory, exist_ok=True)
    prefix = 'fidl.test.dangerous'
    # generate FIDL libraries
    library_names = []
    for style_name, style_func in STYLES:
        for use_name, use_func in USES:
            library_name = '%s.%s.%s' % (prefix, use_name, style_name)
            fidl_file = os.path.join(directory, '%s.test.fidl' % library_name)
            with open(fidl_file, 'w') as f:
                f.write(generated('//'))
                f.write('library %s;\n' % library_name)
                use_func(
                    f, [
                        Identifier(
                            style_func(r.parts), r.tag, r.bindings_denylist)
                        for r in identifier_defs
                    ])
            subprocess.check_output([FIDL_FORMAT, '-i', fidl_file])
            library_names.append(library_name)

    # generate BUILD.gn for FIDL libraries
    build_file = os.path.join(directory, 'BUILD.gn')
    with open(build_file, 'w') as build_gn:
        build_gn.write(generated('#'))
        build_gn.write('import("//build/fidl/fidl.gni")\n\n')
        build_gn.write('group("fidl") {\ndeps=[\n')
        for library_name in library_names:
            build_gn.write('  ":%s",\n' % library_name)
        build_gn.write(']}\n')
        for library_name in library_names:
            build_gn.write(
                'fidl("%s") {\n  sources = [ "%s.test.fidl" ] }\n\n' %
                (library_name, library_name))
    subprocess.check_output([GN, 'format', build_file])

    return library_names


def generate_cpp(libraries: List[str]) -> None:
    directory = os.path.join(DIR, 'cpp')
    os.makedirs(directory, exist_ok=True)

    # generate BUILD.gn for C++ test
    build_file = os.path.join(directory, 'BUILD.gn')
    with open(build_file, 'w') as build_gn:
        build_gn.write(generated('#'))
        for library_name in libraries:
            build_gn.write(
                """source_set("%s_cpp") {
    output_name = "cpp_fidl_dangerous_identifiers_test_%s"
    sources = [ "%s_test.cc" ]
    deps = [
""" % (library_name, library_name, library_name))
            build_gn.write('    "%s",\n' % library_target(library_name))
            build_gn.write('  ]\n}\n')
        build_gn.write("""group("cpp") {
    deps = [""")
        for library_name in libraries:
            build_gn.write("""
        ":%s_cpp",
""" % (library_name))
        build_gn.write("""
    ]
  }""")

    subprocess.check_output([GN, 'format', build_file])


def generate_rust(libraries: List[str]) -> None:
    os.makedirs(os.path.join(DIR, 'rust', 'src'), exist_ok=True)

    # Allowlist of libraries we can compile in Rust
    # TODO(fxbug.dev/60219): Make all libraries pass.
    allowed_libraries = {
            'fidl.test.dangerous.constants.lower',
            'fidl.test.dangerous.constants.camel',
            'fidl.test.dangerous.constants.upper',
            'fidl.test.dangerous.using.lower',
            'fidl.test.dangerous.using.camel',
            'fidl.test.dangerous.using.upper',
            'fidl.test.dangerous.enums.lower',
            'fidl.test.dangerous.struct.types.lower',
            'fidl.test.dangerous.struct.types.upper',
            'fidl.test.dangerous.table.names.lower',
            'fidl.test.dangerous.table.names.camel',
            'fidl.test.dangerous.table.names.upper',
    }

    # BUILD.gn
    build_file = os.path.join(DIR, 'rust', 'BUILD.gn')
    with open(build_file, 'w') as f:
        f.write(generated('#'))
        f.write('\n')
        f.write('import("//build/rust/rustc_test.gni")\n')
        f.write('import("//tools/fidl/measure-tape/measure_tape.gni")\n')
        f.write('\n')
        f.write('rustc_test("rust") {\n')
        f.write('  sources = [ "src/lib.rs" ]\n')
        f.write('  deps = [\n')
        for library_name in libraries:
            f.write('    ')
            if library_name not in allowed_libraries:
                f.write('# ')
            f.write('"%s-rustc",\n' % library_target(library_name))
        f.write('  ]\n')
        f.write('}\n')
    subprocess.check_output([GN, 'format', build_file])

    # lib.rs
    lib_rs = os.path.join(DIR, 'rust', 'src', 'lib.rs')
    with open(lib_rs, 'w') as f:
        f.write(generated('//'))
        f.write('#![cfg(test)]\n')
        f.write('#![allow(unused_imports)]\n')
        f.write('use {\n')
        for library_name in libraries:
            f.write('  ')
            if library_name not in allowed_libraries:
                f.write('// ')
            f.write('fidl_%s,\n' % library_name.replace('.', '_'))
        f.write('};\n')
    subprocess.check_output([RUST_FORMAT, lib_rs])


if __name__ == '__main__':
    identifier_defs = dangerous_identifiers()
    library_names = generate_fidl(identifier_defs)
    generate_cpp(library_names)
    generate_rust(library_names)
