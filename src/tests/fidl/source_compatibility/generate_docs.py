#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import difflib
import json
import os
import subprocess
import sys
import typing

from dataclasses import dataclass, InitVar

# Lines of context to show in diffs
DIFF_CONTEXT = 3

# Languages to specify in markdown for syntax highlighting
MD_LANG = {
    'fidl': 'fidl',
    'go': 'go',
    'rust': 'rust',
    'hlcpp': 'cpp',
}

# Stages of FIDL migration
FIDL_STEPS = ('before', 'during', 'after')

# Bindings
BINDINGS = ('hlcpp', 'go', 'rust')


def remove_boilerplate(language: str,
                       lines: typing.List[str]) -> typing.List[str]:
    '''Remove boilerplate lines.'''
    if language == 'fidl':
        # skip copyright, library line
        while (not lines[0].strip() or lines[0].startswith('//') or
               lines[0].startswith('library ')):
            lines.pop(0)
        return lines
    if language == 'hlcpp':
        # skip copyright, includes, namespace lines
        while (not lines[0].strip() or lines[0].startswith('//') or
               lines[0].startswith('#include ') or
               lines[0].startswith('namespace ')):
            lines.pop(0)
        # skip main and everything after
        for i in range(len(lines)):
            if lines[i].startswith('int main('):
                return lines[:i]
        return lines
    if language == 'rust':
        # skip copyright, compiler directives and use lines
        # TODO: skip use block
        while (not lines[0].strip() or lines[0].startswith('//') or
               lines[0].startswith('#![') or lines[0].startswith('use ')):
            lines.pop(0)
        # skip main and everything after
        for i in range(len(lines)):
            if lines[i].startswith('fn main()'):
                return lines[:i]
        return lines
    if language == 'go':
        # skip copyright and package line
        while (not lines[0].strip() or lines[0].startswith('//') or
               lines[0].startswith('package ')):
            lines.pop(0)
        # skip import block
        if lines[0].startswith('import ('):
            while not lines[0].startswith(')'):
                lines.pop(0)
            lines.pop(0)
        # skip blank lines
        while not lines[0].strip():
            lines.pop(0)
        # skip main and everything after
        for i in range(len(lines)):
            if lines[i].startswith('func main()'):
                return lines[:i]
        return lines
    raise NotImplementedError(f'Unknown binding/language {language}.')


def cat(out: typing.IO, language: str, filename: str):
    '''Render contents of filename to out'''
    with open(filename) as source:
        lines = remove_boilerplate(language, source.readlines())
        out.write(f'```{MD_LANG[language]}\n{"".join(lines)}\n```\n')


def diff(out: typing.IO, language: str, pre: str, post: str):
    '''
    Render a diff of pre and post to out.
    '''
    pre_lines = remove_boilerplate(language, open(pre).readlines())
    post_lines = remove_boilerplate(language, open(post).readlines())

    matcher = difflib.SequenceMatcher(
        None, pre_lines, post_lines, autojunk=False)
    for opcodes in matcher.get_grouped_opcodes(DIFF_CONTEXT):
        out.write('```diff\n')
        for tag, pre_start, pre_end, post_start, post_end in opcodes:
            if tag == 'equal':
                for line in pre_lines[pre_start:pre_end]:
                    out.write('  ' + line)
                continue
            if tag in {'replace', 'delete'}:
                for line in pre_lines[pre_start:pre_end]:
                    out.write('- ' + line)
            if tag in {'replace', 'insert'}:
                for line in post_lines[post_start:post_end]:
                    out.write('+ ' + line)
        out.write('\n```\n')


@dataclass
class FidlStep:
    base_dir: InitVar[str]
    name: str
    source: str
    instructions: typing.Optional[typing.List[str]] = None

    def __post_init__(self, base_dir):
        self.source = os.path.join(base_dir, self.source)

    def is_initial(self) -> bool:
        return self.name == 'before'


@dataclass
class BindingStep:
    base_dir: InitVar[str]
    binding: str
    source: typing.Optional[str] = None
    fidl: typing.Optional[str] = None
    instructions: typing.Optional[typing.List[str]] = None

    def __post_init__(self, base_dir):
        if self.source is not None:
            self.source = os.path.join(base_dir, self.source)

    def is_source(self) -> bool:
        return self.fidl is None

    def is_fidl(self) -> bool:
        return self.source is None

    def is_initial(self) -> bool:
        return self.fidl is not None and self.source is not None

    def get_source(self) -> str:
        assert self.source is not None
        return self.source


@dataclass
class Config:
    title: str
    fidl: typing.List[FidlStep]
    binding_names: typing.List[str]
    bindings: typing.Dict[str, typing.List[BindingStep]]


def read_config(filename: str) -> Config:
    '''Read configuration from a JSON file into Python types'''
    base_dir = os.path.dirname(filename)
    # read the raw config
    raw_config = json.load(open(filename, 'rt'))
    # the bindings that are actually present in this config
    bindings = [b for b in BINDINGS if b in raw_config]
    return Config(
        title=raw_config['title'],
        fidl=[
            FidlStep(base_dir, f, **raw_config['fidl'][f]) for f in FIDL_STEPS
        ],
        binding_names=bindings,
        bindings={
            b: [BindingStep(base_dir, b, **s) for s in raw_config[b]
               ] for b in bindings
        })


def config_deps(config: Config) -> typing.List[str]:
    '''
    List files referred to by this configuration file.
    '''
    deps = [f.source for f in config.fidl]
    for binding in config.bindings.values():
        deps.extend(step.source for step in binding if step.source)

    return deps


def generate_docs(config: Config, out: typing.IO):
    '''
    Generate transition documentation.
    '''
    # Title
    out.write(f'# {config.title}\n')

    # Track last transition state to allow diffing
    previous_fidl = config.fidl.pop(0)
    previous_sources = {
        b: config.bindings[b].pop(0) for b in config.binding_names
    }

    # Initial state
    out.write('## Initial State\n')

    out.write('### FIDL Library\n')
    cat(out, 'fidl', previous_fidl.source)

    for b in config.binding_names:
        out.write(f'### {b}\n')
        cat(out, b, previous_sources[b].get_source())

    while len(config.fidl) or any(len(b) for b in config.bindings.values()):
        # Source changes, if any...
        source_changes: typing.Dict[str, BindingStep] = {
            b: s.pop(0)
            for b, s in config.bindings.items()
            if len(s) and s[0].is_source()
        }
        if len(source_changes):
            out.write('## Update Source Code\n')
            for b in config.binding_names:
                if b in source_changes:
                    out.write(f'### {b}\n')
                    for i in source_changes[b].instructions or []:
                        out.write(f'- {i}\n')
                    diff(
                        out, b, previous_sources[b].get_source(),
                        source_changes[b].get_source())
            previous_sources.update(source_changes)

        # Fidl changes
        if len(config.fidl):
            fidl = config.fidl.pop(0)
            out.write('## Update FIDL Library\n')
            for i in fidl.instructions or []:
                out.write(f'- {i}\n')
            diff(out, 'fidl', previous_fidl.source, fidl.source)
            for steps in config.bindings.values():
                step = steps.pop(0)
                assert step.is_fidl() and step.fidl == fidl.name
            previous_fidl = fidl


if __name__ == '__main__':
    argp = argparse.ArgumentParser(
        description='Generate source transition docs.')
    argp.add_argument(
        '--json', required=True, help='JSON description of the test')
    argp.add_argument('--out', required=True, help='Markdown file to write')
    argp.add_argument(
        '--depfile', required=True, help='Dependency file to write')
    args = argp.parse_args()

    config = read_config(args.json)

    deps = config_deps(config)

    with open(args.out, 'wt') as out:
        generate_docs(config, out)

    with open(args.depfile, 'wt') as depfile:
        depfile.write(f'{args.out}: {" ".join(deps)}\n')