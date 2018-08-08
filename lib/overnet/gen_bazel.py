#!/usr/bin/env python2.7

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import os
import sys

# This program generates BUILD.bazel, WORKSPACE, .bazelrc from BUILD.gn

####################################################################################################
# TOKENIZER

Tok = collections.namedtuple('Tok', ['tok', 'value'])


def is_ident_start(c):
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or c == '_'


def is_ident_char(c):
    return is_ident_start(c) or is_digit(c)


def is_digit(c):
    return c >= '0' and c <= '9'


def is_whitespace(c):
    return c in ' \t\r\n'


sym_name = {
    ',': 'comma',
    '(': 'left_paren',
    ')': 'right_paren',
    '{': 'left_mustache',
    '}': 'right_mustache',
    '[': 'left_square',
    ']': 'right_square',
    '=': 'equals',
}


def is_symbol(c):
    return c in sym_name.keys()


def tok(s):
    if s == '':
        return [], s
    c = s[0]
    if is_ident_start(c):
        return tok_ident(s)
    if c == '#':
        return tok_comment(s)
    if is_whitespace(c):
        return tok_whitespace(s)
    if is_symbol(c):
        return tok_cont(Tok(sym_name[c], c), s[1:])
    if c == '"':
        return tok_string(s[1:])
    print 'bad character: ' + s[0]
    sys.exit(1)


def tok_cont(token, s):
    toks, rest = tok(s)
    return [token] + toks, rest


def tok_comment(s):
    while s != '' and s[0] != '\n':
        s = s[1:]
    return tok(s[1:])


def tok_ident(s):
    ident = ''
    while s and is_ident_char(s[0]):
        ident += s[0]
        s = s[1:]
    return tok_cont(Tok('ident', ident), s)


def tok_string(s):
    string = ''
    while s[0] != '"':
        string += s[0]
        s = s[1:]
    return tok_cont(Tok('string', string), s[1:])


def tok_whitespace(s):
    while s and is_whitespace(s[0]):
        s = s[1:]
    return tok(s)


def tokenize(s):
    toks, rest = tok(s)
    if rest != '':
        print 'dangling: ' + rest
        sys.exit(1)
    return toks


####################################################################################################
# PARSER

Bundle = collections.namedtuple('Bundle', ['rule', 'name', 'values'])


def take(toks, tok):
    if toks[0].tok != tok:
        print 'expected %s, got %s' % (tok, toks[0].tok)
        sys.exit(1)
    return toks.pop(0).value


def maybe_take(toks, tok):
    if toks[0].tok != tok:
        return None
    return toks.pop(0).value


def parse_dict(toks):
    d = {}
    while not maybe_take(toks, 'right_mustache'):
        key = take(toks, 'ident')
        take(toks, 'equals')
        value = parse_value(toks)
        d[key] = value
    return d


def parse_list(toks):
    l = []
    while not maybe_take(toks, 'right_square'):
        l.append(parse_value(toks))
        if not maybe_take(toks, 'comma'):
            take(toks, 'right_square')
            break
    return l


def parse_value(toks):
    if maybe_take(toks, 'left_mustache'):
        return parse_dict(toks)
    if maybe_take(toks, 'left_square'):
        return parse_list(toks)
    s = maybe_take(toks, 'string')
    if s is not None:
        return s
    s = maybe_take(toks, 'ident')
    if s is not None:
        if s == 'true':
            return True
        if s == 'false':
            return False
        print 'bad ident in value position: ' + s
    print 'bad value token: %r' % toks


def parse(toks):
    bundles = []
    while toks:
        rule = take(toks, 'ident')
        take(toks, 'left_paren')
        name = take(toks, 'string')
        take(toks, 'right_paren')
        body = None
        if maybe_take(toks, 'left_mustache'):
            body = parse_dict(toks)
        bundles.append(Bundle(rule, name, body))
    return bundles


####################################################################################################
# CODEGEN

def mapdep(n):
    if n[0] == ':':
        return n
    m = {
        '//third_party/googletest:gtest': '@com_google_googletest//:gtest',
        '//third_party/googletest:gmock': None,
    }
    return m[n]


FUZZERS = ['bbr', 'internal_list', 'linearizer',
           'packet_protocol', 'receive_mode', 'routing_header']

assert FUZZERS == sorted(FUZZERS)


with open('BUILD.bazel', 'w') as o:
    with open('BUILD.gn') as f:
        for bundle in parse(tokenize(f.read())):
            if bundle.rule == 'source_set':
                print >>o, 'cc_library('
                print >>o, '  name="%s",' % bundle.name
                print >>o, '  srcs=[%s],' % ','.join(
                    '"%s"' % s for s in bundle.values['sources'])
                if 'deps' in bundle.values:
                    print >>o, '  deps=[%s],' % ','.join(
                        '"%s"' % mapdep(s) for s in bundle.values['deps'] if mapdep(s) is not None)
                print >>o, ')'
            if bundle.rule == 'executable':
                if bundle.values.get('testonly', False):
                    print >>o, 'cc_test('
                else:
                    print >>o, 'cc_binary('
                print >>o, '  name="%s",' % bundle.name
                print >>o, '  srcs=[%s],' % ','.join(
                    '"%s"' % s for s in bundle.values['sources'])
                print >>o, '  deps=[%s],' % ','.join(
                    '"%s"' % mapdep(s) for s in bundle.values['deps'] if mapdep(s) is not None)
                print >>o, ')'
    for fuzzer in FUZZERS:
        print >>o, 'cc_binary('
        print >>o, '  name="%s_fuzzer",' % fuzzer
        srcs = ['%s_fuzzer.cc' % fuzzer]
        helpers_h = '%s_fuzzer_helpers.h' % fuzzer
        if os.path.exists(helpers_h):
            srcs.append(helpers_h)
        print >>o, '  srcs=[%s],' % ', '.join('"%s"' % s for s in srcs)
        print >>o, '  deps=[":overnet", ":test_util"],'
        print >>o, ')'

WORKSPACE = """
# This file is not checked in, but generated by gen_bazel.py
# Make changes there

git_repository(
    name = 'com_google_googletest',
    remote = 'https://github.com/google/googletest.git',
    commit = 'd5266326752f0a1dadbd310932d8f4fd8c3c5e7d',
)
"""

BAZELRC = """
# This file is not checked in, but generated by gen_bazel.py
# Make changes there

build --client_env=CC=clang
build --copt -std=c++14

build:asan --strip=never
build:asan --copt -fsanitize=address
build:asan --copt -O0
build:asan --copt -fno-omit-frame-pointer
build:asan --linkopt -fsanitize=address
build:asan --action_env=ASAN_OPTIONS=detect_leaks=1:color=always
build:asan --action_env=LSAN_OPTIONS=report_objects=1

build:asan-fuzzer --strip=never
build:asan-fuzzer --copt -fsanitize=fuzzer,address
build:asan-fuzzer --copt -fsanitize-coverage=trace-cmp
build:asan-fuzzer --copt -O0
build:asan-fuzzer --copt -fno-omit-frame-pointer
build:asan-fuzzer --linkopt -fsanitize=fuzzer,address
build:asan-fuzzer --action_env=ASAN_OPTIONS=detect_leaks=1:color=always
build:asan-fuzzer --action_env=LSAN_OPTIONS=report_objects=1

build:msan --strip=never
build:msan --copt -fsanitize=memory
build:msan --copt -O0
build:msan --copt -fsanitize-memory-track-origins
build:msan --copt -fsanitize-memory-use-after-dtor
build:msan --copt -fno-omit-frame-pointer
build:msan --copt -fPIC
build:msan --linkopt -fsanitize=memory
build:msan --linkopt -fPIC
build:msan --action_env=MSAN_OPTIONS=poison_in_dtor=1

build:tsan --strip=never
build:tsan --copt -fsanitize=thread
build:tsan --copt -fno-omit-frame-pointer
build:tsan --copt -DNDEBUG
build:tsan --linkopt -fsanitize=thread
build:tsan --action_env=TSAN_OPTIONS=halt_on_error=1

build:ubsan --strip=never
build:ubsan --copt -fsanitize=undefined
build:ubsan --copt -fno-omit-frame-pointer
build:ubsan --copt -DNDEBUG
build:ubsan --copt -fno-sanitize=function,vptr
build:ubsan --linkopt -fsanitize=undefined
build:ubsan --action_env=UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

build:ubsan-fuzzer --strip=never
build:ubsan-fuzzer --copt -fsanitize=fuzzer,undefined
build:ubsan-fuzzer --copt -fno-omit-frame-pointer
build:ubsan-fuzzer --copt -DNDEBUG
build:ubsan-fuzzer --copt -fno-sanitize=function,vptr
build:ubsan-fuzzer --linkopt -fsanitize=fuzzer,undefined
build:ubsan-fuzzer --action_env=UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
"""

with open('WORKSPACE', 'w') as o:
    o.write(WORKSPACE)

with open('.bazelrc', 'w') as o:
    o.write(BAZELRC)
