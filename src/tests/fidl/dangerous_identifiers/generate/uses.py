# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

__all__ = ["USES"]

from typing import List

from common import *

# Define places that identifiers may appear in a FIDL library:
USES: List[Use] = []


def use(func):
    USES.append(Use(func.__name__.replace("_", "."), (func,)))


# TODO(fxbug.dev/77561): we probably want to add a "constraints" test, for cases
#  like `vector<Foo>:true` etc.


@use
def constants(f, idents: List[ScopedIdentifier]):
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"const {ident} uint32 = 1;\n")


@use
def using(f, idents: List[ScopedIdentifier]):
    for ident in idents:
        # TODO(fxbug.dev/8042): Having a declaration with same same name as what is
        # aliased causes a cycle.
        if ident.name == "string":
            continue
        f.write(ident.decl_attributes)
        f.write(f"alias {ident} = string;\n")


# TODO(ianloic): Make this test work. It requires N libraries to import for N
# identifiers. That doesn't fit well into the model of this test.
# @use
# def using_as(f, idents):
#  for ident in idents:
#    f.write('using fuchsia.mem as %s;\n' % ident)


@use
def enums(f, idents: List[ScopedIdentifier]):
    # enums with every dangerous name
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"type {ident} = strict enum {{ MEMBER = 1; }};\n")

    # enum with every dangerous field name
    f.write("type DangerousMembers = strict enum {\n")
    for i, ident in enumerate(idents):
        f.write(ident.decl_attributes)
        f.write(f"  {ident} = {i};\n")
    f.write("};\n")


@use
def struct_types(f, idents: List[ScopedIdentifier]):
    # structs with every dangerous name
    f.write("alias membertype = uint32;\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"type {ident} = struct {{ member membertype; }};\n")

    # a struct with every dangerous name as the field type
    f.write("type DangerousMembers = struct {\n")
    for i, ident in enumerate(idents):
        # dangerous field type
        f.write(ident.decl_attributes)
        f.write(f"  f{i} {ident};\n")
    f.write("};\n")


@use
def struct_names(f, idents: List[ScopedIdentifier]):
    # a struct with every dangerous name as the field name
    f.write("type DangerousMembers = struct {\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"  {ident} uint32;\n")
    f.write("};\n")


@use
def union_names(f, idents):
    # unions with every dangerous name
    f.write("type membertype = struct {};\n")
    for ident in idents:
        f.write(f"type {ident} = strict union {{ 1: member membertype; }};\n")

    # a union with every dangerous name as the field type
    f.write("type DangerousMembers = strict union {\n")
    for i, ident in enumerate(idents):
        # dangerous field type
        f.write(f"  {i+1}: f{i} {ident};\n")
    f.write("};\n")


@use
def union_types(f, idents):
    # a union with every dangerous name as the field name
    f.write("type DangerousMembers = strict union {\n")
    for i, ident in enumerate(idents):
        f.write(f"  {i+1}: f{i} uint32;\n")
    f.write("};\n")


@use
def table_names(f, idents: List[ScopedIdentifier]):
    # tables with every dangerous name
    f.write("alias membertype = uint32;\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"type {ident} = table {{ 1: member membertype; }};\n")
    # a table with every dangerous name as the field type
    f.write("type DangerousMembers = table {\n")
    for i, ident in enumerate(idents):
        # dangerous field type
        f.write(ident.decl_attributes)
        f.write(f"  {i+1}: f{i} {ident};\n")
    f.write("};\n")


@use
def table_fields(f, idents: List[ScopedIdentifier]):
    # a table with every dangerous name as the field name
    f.write("type DangerousMembers = table {\n")
    for i, ident in enumerate(idents):
        f.write(ident.decl_attributes)
        f.write(f"  {i+1}: {ident} uint32;\n")
    f.write("};\n")


@use
def protocol_names(f, idents: List[ScopedIdentifier]):
    # a protocols with every dangerous name
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"protocol {ident} {{ JustOneMethod(); }};\n")


@use
def method_names(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a method name
    f.write("protocol DangerousMethods {\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"  {ident}();\n")
    f.write("};\n")


@use
def event_names(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as an event name
    f.write("protocol DangerousEvents {\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"  -> {ident}();\n")
    f.write("};\n")


@use
def method_request_arguments(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a request argument
    f.write("alias argtype = uint32;\n")
    f.write("protocol DangerousRequestArguments {\n")
    for i, ident in enumerate(idents):
        f.write(ident.decl_attributes)
        f.write(f"  Method{i}(struct {{ {ident} argtype; }});\n")
    f.write("};\n")


@use
def method_response_arguments(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a response argument
    f.write("alias argtype = uint32;\n")
    f.write("protocol DangerousResponseArguments {\n")
    for i, ident in enumerate(idents):
        f.write(ident.decl_attributes)
        f.write(f"  Method{i}() -> (struct {{ {ident} argtype; }});\n")
    f.write("};\n")


@use
def method_event_arguments(f, idents: List[ScopedIdentifier]):
    # a protocol with every dangerous name as a event argument
    f.write("alias argtype = uint32;\n")
    f.write("protocol DangerousResponseArguments {\n")
    for i, ident in enumerate(idents):
        f.write(ident.decl_attributes)
        f.write(f"  -> Event{i}(struct {{ {ident} argtype; }});\n")
    f.write("};\n")


@use
def service_names(f, idents: List[ScopedIdentifier]):
    # a service with every dangerous name
    f.write("protocol SampleProtocol { Method(); };\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"service {ident} {{ member client_end:SampleProtocol; }};\n")


@use
def service_member_types(f, idents: List[ScopedIdentifier]):
    # protocols with every dangerous name
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"protocol {ident} {{ JustOneMethod(); }};\n")

    # a service with every dangerous name as the member type
    f.write("service DangerousServiceMemberTypes {\n")
    for i, ident in enumerate(idents):
        # dangerous field type
        f.write(ident.decl_attributes)
        f.write(f"  f{i} client_end:{ident};\n")
    f.write("};\n")


@use
def service_member_names(f, idents: List[ScopedIdentifier]):
    # a service with every dangerous name as the member name
    f.write("protocol SampleProtocol { Method(); };\n")
    f.write("service DangerousServiceMemberNames {\n")
    for ident in idents:
        f.write(ident.decl_attributes)
        f.write(f"  {ident} client_end:SampleProtocol;\n")
    f.write("};\n")
