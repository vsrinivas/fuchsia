# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

__all__ = ['USES']

from typing import List

from generate.types import *

# Define places that identifiers may appear in a FIDL library:
USES: List[Use] = []


def use(func):
    USES.append(Use(func.__name__.replace('_', '.'), (func,)))


@use
def constants(f, idents: List[ScopedIdentifier]):
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'const uint32 {ident} = 1;\n')


@use
def using(f, idents: List[ScopedIdentifier]):
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'using {ident} = vector;\n')


# TODO(ianloic): Make this test work. It requires N libraries to import for N
# identifiers. That doesn't fit well into the model of this test.
#@use
#def using_as(f, idents):
#  for ident in idents:
#    f.write('using fuchsia.mem as %s;\n' % ident)


@use
def enums(f, idents: List[ScopedIdentifier]):
    # enums with every dangerous name
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'enum {ident} {{ MEMBER = 1; }};\n')

    # enum with every dangerous field name
    f.write('enum DangerousMembers {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  {ident} = {ident.tag - 1};\n')
    f.write('};\n')


@use
def struct_types(f, idents: List[ScopedIdentifier]):
    # structs with every dangerous name
    f.write('using membertype = uint32;\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'struct {ident} {{ membertype member = 1; }};\n')

    # a struct with every dangerous name as the field type
    f.write('struct DangerousMembers {\n')
    for ident in idents:
        # dangerous field type
        f.write(ident.decl_attributes)
        f.write(f'  {ident} f{ident.tag - 1};\n')
    f.write('};\n')


@use
def struct_names(f, idents: List[ScopedIdentifier]):
    # a struct with every dangerous name as the field name
    f.write('struct DangerousMembers {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  uint32 {ident};\n')
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
def table_names(f, idents: List[ScopedIdentifier]):
    # tables with every dangerous name
    f.write('using membertype = uint32;\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'table {ident} {{ 1: membertype member; }};\n')
    # a table with every dangerous name as the field type
    f.write('table DangerousMembers {\n')
    next_tag = 1
    for ident in sorted(idents, key=lambda ident: ident.tag):
        # dangerous field type
        while ident.tag > next_tag:
            f.write(f'  {next_tag}: reserved;\n')
            next_tag = next_tag + 1
        f.write(ident.decl_attributes)
        f.write(f'  {ident.tag}: {ident} f{ident.tag-1};\n')
        next_tag = ident.tag + 1
    f.write('};\n')


@use
def table_fields(f, idents: List[ScopedIdentifier]):
    # a table with every dangerous name as the field name
    f.write('table DangerousMembers {\n')
    next_tag = 1
    for ident in sorted(idents, key=lambda ident: ident.tag):
        while ident.tag > next_tag:
            f.write(f'  {next_tag}: reserved;\n')
            next_tag = next_tag + 1
        f.write(ident.decl_attributes)
        f.write(f'  {ident.tag}: uint32 {ident};\n')
        next_tag = ident.tag + 1
    f.write('};\n')


@use
def protocol_names(f, idents: List[ScopedIdentifier]):
    # a protocols with every dangerous name
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'protocol {ident} {{ JustOneMethod(); }};\n')


@use
def method_names(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a method name
    f.write('protocol DangerousMethods {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  {ident}();\n')
    f.write('};\n')


@use
def event_names(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as an event name
    f.write('protocol DangerousEvents {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  -> {ident}();\n')
    f.write('};\n')


@use
def method_request_arguments(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a request argument
    f.write('using argtype = uint32;\n')
    f.write('protocol DangerousRequestArguments {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  Method{ident.tag - 1}(argtype {ident});\n')
    f.write('};\n')


@use
def method_response_arguments(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a response argument
    f.write('using argtype = uint32;\n')
    f.write('protocol DangerousResponseArguments {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  Method{ident.tag - 1}() -> (argtype {ident});\n')
    f.write('};\n')


@use
def method_event_arguments(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a event argument
    f.write('using argtype = uint32;\n')
    f.write('protocol DangerousResponseArguments {\n')
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f'  -> Event{ident.tag - 1}(argtype {ident});\n')
    f.write('};\n')
