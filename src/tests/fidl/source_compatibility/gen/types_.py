# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" The types in this file correspond directly to the test.json structure. """

from dataclasses import asdict, dataclass
from typing import Dict, List, Union

# These string values are not only used in the JSON, but also as the test
# subdirectory names.
HLCPP = 'hlcpp'
LLCPP = 'llcpp'
RUST = 'rust'
DART = 'dart'
GO = 'go'
BINDINGS = [HLCPP, DART, LLCPP, RUST, GO]

# Path relative to the test root.
RelativePath = str


@dataclass
class FidlDef:
    """
    Definition of one state of the FIDL library within the transition. Defined
    by a FIDL file located at |source|, and may have accompanying |instructions|
    describing the change between this state and the previous one.
    """
    source: RelativePath
    instructions: List[str]

    @classmethod
    def fromdict(cls, data: dict):
        return FidlDef(data['source'], data['instructions'])


# A reference to a FidlDef. The individual steps for each binding needs to be
# able to refer to a state of the FIDL library. Since these states are shared,
# they are defined once separately (with FidlDef) and referenced elsewhere using
# FidlRefs.
FidlRef = str


@dataclass
class FidlStep:
    """ A step representing a change to the FIDL library. """
    step_num: int
    fidl: FidlRef

    @classmethod
    def fromdict(cls, data: dict):
        return FidlStep(data['step_num'], data['fidl'])


@dataclass
class SourceStep:
    """ A step representing a change to a single binding. """
    step_num: int
    source: RelativePath
    instructions: List[str]

    @classmethod
    def fromdict(cls, data: dict):
        return SourceStep(
            data['step_num'], data['source'], data['instructions'])


@dataclass
class Steps:
    """
    A sequence of steps describing the transition for a single binding. It
    consists of a starting FIDL and source states, followed by a sequence of
    alternative FIDL/source changes (defined in |steps|).
    """
    starting_fidl: FidlRef
    starting_src: RelativePath
    steps: List[Union[FidlStep, SourceStep]]

    @classmethod
    def fromdict(cls, data: dict):
        return cls(
            data['starting_fidl'], data['starting_src'], [
                FidlStep.fromdict(s) if 'fidl' in s else SourceStep.fromdict(s)
                for s in data['steps']
            ])


@dataclass
class CompatTest:
    """
    A source compatibility test, which describes a transition involving changes
    to a FIDL library and user code in each binding that uses that library.
    Fields:
        title: A human readable title for the test. Used in the generated
               documentation.
        fidl: Definition of the states of the FIDL library during the
              transition.
        bindings: Definition of the source/FIDL steps taken by each binding
                  for this transition.
    """
    title: str
    fidl: Dict[FidlRef, FidlDef]
    bindings: Dict[str, Steps]

    @classmethod
    def fromdict(cls, data: dict):
        return CompatTest(
            data['title'],
            {k: FidlDef.fromdict(v) for k, v in data['fidl'].items()},
            {k: Steps.fromdict(v) for k, v in data['bindings'].items()})

    def todict(self):
        return asdict(self)
