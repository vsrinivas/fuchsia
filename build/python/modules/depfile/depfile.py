# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Python module for creating depfiles that can be read by ninja.

ninja supports depfiles for tracking implicit dependencies that are needed by
future incremental rebuilds:  https://ninja-build.org/manual.html#_depfile

The format of these is a Makefile, with a single output listed (see
https://gn.googlesource.com/gn/+/main/docs/reference.md#var_depfile)

Examples:

The simplest form is a single line for output and each inputs::

    path/to/an/output:  path/to/input_a, path/to/input_b

paths are all relative to the root build dir (GN's root_build_dir)

Ninja also supports a multiline format which uses backslashes as a continuation
character::

    path/to/an/output: \
        path/to/input_a \
        path/to/input_b \

For readability, this is the format that is used by this module.

Basic usage:

>>> dep_file = DepFile("path/to/output")
>>> dep_file.add_input("path/to/input_a")
>>> dep_file.add_input("path/to/input_b")
>>> print(dep_file)
path/to/output: \
    path/to/input_a \
    path/to/input_b \

>>>

By default, paths are made relative to the current working directory, but the
paths can all be rebased (made relative from) a different absolute path:

Assuming that the current working dir is ``/foo/bar``, and the paths to the
output and inputs are relative

>>> dep_file = DepFile("baz/melon/output", rebase="/foo/bar/baz")
>>> dep_file.add_input("baz/input_a")
>>> dep_file.add_input("/foo/bar/baz/input_b")
>>> dep_file.add_input("/foo/bar/monkey/panda/input_c")
>>> print(dep_file)
melon/output: \
    input_a input_b \
    ../monkey/panda/input/_c \

>>>
"""
from os import PathLike
import os
from typing import Iterable, Set, Union
FilePath = Union[str, PathLike]


class DepFile:
    """A helper class for collecting implicit dependencies and writing them to
    depfiles that ninja can read.

    Each DepFile instance supports collecting the inputs used to create a single
    output file.
    """

    def __init__(self, output: FilePath, rebase: FilePath = None) -> None:
        if rebase is not None:
            self.rebase_from = rebase
        else:
            self.rebase_from = os.getcwd()
        self.output = self._rebase(output)
        self.deps: Set[FilePath] = set()

    def _rebase(self, path: FilePath) -> FilePath:
        return os.path.relpath(path, start=self.rebase_from)

    def add_input(self, input: FilePath) -> None:
        """Add an input to the depfile"""
        self.deps.add(self._rebase(input))

    def update(self, other: Union['DepFile', Iterable[FilePath]]) -> None:
        """Add each input to this depfile"""
        # If other is another DepFile, just snag the values from it's internal
        # dict.
        if isinstance(other, self.__class__):
            inputs = other.deps
        else:
            inputs: Iterable[FilePath] = other

        # Rebase them all in a bulk operation.
        inputs = [self._rebase(input) for input in inputs]

        # And then update the set of deps.
        for input in inputs:
            self.deps.add(input)

    @classmethod
    def from_deps(
            cls,
            output: FilePath,
            inputs: Iterable[FilePath],
            rebase: FilePath = None) -> 'DepFile':
        dep_file = cls(output, rebase)
        dep_file.update(inputs)
        return dep_file

    def __repr__(self) -> str:
        return "{}: \\\n  {}\n".format(
            self.output, " \\\n  ".join(sorted(self.deps)))

    def write_to(self, file) -> None:
        """Write out the depfile contents to the given writeable `file-like` object.
        """
        file.write(str(self))
