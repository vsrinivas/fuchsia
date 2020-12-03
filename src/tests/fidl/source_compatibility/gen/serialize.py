# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" Converting to and from the JSON description of a test. """

import json
from pathlib import Path
from typing import Dict, List, Tuple

from transitions import Binding, State, Transition, Type
from util import to_fidl_filename, to_fidl_name, to_src_filename, from_src_filename

STEPS_FILE = 'test.json'


def write_transitions(
        root: Path, transitions_by_binding: Dict[Binding, Transition]):
    path = root / STEPS_FILE
    with open(path, 'w+') as f:
        contents = serialize_transitions(transitions_by_binding)
        f.write(json.dumps(contents, indent=2))


def read_transitions(root: Path) -> Dict[Binding, Transition]:
    contents = open(root / STEPS_FILE, 'r').read()
    return deserialize_transitions(json.loads(contents))


def serialize_transitions(
        transitions_by_binding: Dict[Binding, Transition]) -> dict:
    """ Describe the test in a format that GN understands.

    The return value is serializable to JSON.
    """
    fidls = set()
    for _, transition in transitions_by_binding.items():
        fidls.add(transition.starting_fidl)
        fidls.update(
            state for (type_, state) in transition.changes
            if type_ == Type.FIDL)

    result = {
        'title': 'TODO',
        'fidl':
            {
                to_fidl_name(state): {
                    'source': str(to_fidl_filename(state))
                } for state in fidls
            },
    }
    for binding, transition in transitions_by_binding.items():
        changes = [
            {
                'fidl':
                    str(to_fidl_name(transition.starting_fidl)),
                'source':
                    str(to_src_filename(binding, transition.starting_src)),
            }
        ]
        changes.extend(
            [serialize_change(binding, c) for c in transition.changes])
        result[binding.value] = changes
    return result


def deserialize_transitions(data: dict) -> Dict[Binding, Transition]:
    transitions_by_binding = {}
    for binding in Binding:
        transition = data.get(binding.value, None)
        if transition is None:
            continue
        transitions_by_binding[binding] = Transition(
            starting_fidl=State(transition[0]['fidl']),
            starting_src=from_src_filename(transition[0]['source']),
            changes=tuple(deserialize_change(c) for c in transition[1:]))
    return transitions_by_binding


def serialize_change(binding: Binding, change: Tuple[Type, State]) -> dict:
    type_, state = change
    return {
        'fidl': to_fidl_name(state)
    } if type_ == Type.FIDL else {
        'source': str(to_src_filename(binding, state)),
    }


def deserialize_change(change: dict) -> Tuple[Type, State]:
    return (Type.FIDL, State(change['fidl'])) if 'fidl' in change else (
        Type.SOURCE, from_src_filename(change['source']))


def to_flags(transitions_by_binding: Dict[Binding, Transition]) -> str:
    flags = [
        f'--{binding.value}={str(transition)}'
        for binding, transition in transitions_by_binding.items()
    ]
    return ' '.join(flags)
