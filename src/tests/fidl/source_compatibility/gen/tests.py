# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re
import unittest

from gen import weave_steps
from serialize import serialize_transitions, deserialize_transitions, to_flags
from steps import InitializeStep, AddStep
from transitions import Binding, FIDL_ASSISTED, MIXED, SOURCE_ASSISTED, SOURCE_COMPATIBLE


class TestWeaveSteps(unittest.TestCase):

    def test_basic(self):
        result = weave_steps(
            'foo', {
                Binding.HLCPP: SOURCE_COMPATIBLE,
                Binding.LLCPP: SOURCE_COMPATIBLE,
            })

        self.assertEqual(len(result), 5)
        self.assertInitializeStep(result[0], 'fidl/during.test.fidl')
        self.assertInitializeStep(result[1], 'hlcpp/before.cc')
        self.assertInitializeStep(result[2], 'llcpp/before.cc')
        self.assertAddStep(
            result[3], from_file='hlcpp/before.cc', to_file='hlcpp/after.cc')
        self.assertAddStep(
            result[4], from_file='llcpp/before.cc', to_file='llcpp/after.cc')

    def test_mixed(self):
        result = weave_steps(
            'foo', {
                Binding.GO: FIDL_ASSISTED,
                Binding.RUST: MIXED,
            })

        self.assertEqual(len(result), 8)
        self.assertInitializeStep(result[0], 'fidl/before.test.fidl')
        self.assertInitializeStep(result[1], 'rust/before.rs')
        self.assertInitializeStep(result[2], 'go/before.go')
        self.assertAddStep(
            result[3], from_file='rust/before.rs', to_file='rust/during.rs')

        self.assertAddStep(
            result[4],
            from_file='fidl/before.test.fidl',
            to_file='fidl/during.test.fidl')
        self.assertAddStep(
            result[5], from_file='go/before.go', to_file='go/after.go')

        self.assertAddStep(
            result[6],
            from_file='fidl/during.test.fidl',
            to_file='fidl/after.test.fidl')
        self.assertAddStep(
            result[7], from_file='rust/during.rs', to_file='rust/after.rs')

    def assertInitializeStep(self, step, path):
        self.assertIsInstance(step, InitializeStep)
        self.assertEqual(str(step.path), path)

    def assertAddStep(self, step, from_file, to_file):
        self.assertIsInstance(step, AddStep)
        self.assertEqual(str(step.from_file), from_file)
        self.assertEqual(str(step.to_file), to_file)


class TestSerialize(unittest.TestCase):
    maxDiff = None

    def test_serialize_deserialize(self):
        data = {
            Binding.GO: FIDL_ASSISTED,
            Binding.RUST: MIXED,
        }
        serialized = {
            'title':
                'TODO',
            'fidl':
                {
                    'before': {
                        'source': 'fidl/before.test.fidl'
                    },
                    'during': {
                        'source': 'fidl/during.test.fidl'
                    },
                    'after': {
                        'source': 'fidl/after.test.fidl'
                    },
                },
            "go":
                [
                    {
                        "fidl": "before",
                        "source": "go/before.go"
                    }, {
                        "fidl": "during"
                    }, {
                        "source": "go/after.go",
                    }, {
                        "fidl": "after"
                    }
                ],
            "rust":
                [
                    {
                        "fidl": "before",
                        "source": "rust/before.rs"
                    }, {
                        "source": "rust/during.rs",
                    }, {
                        "fidl": "during"
                    }, {
                        "fidl": "after"
                    }, {
                        "source": "rust/after.rs",
                    }
                ]
        }
        self.assertDictEqual(serialize_transitions(data), serialized)
        self.assertEqual(deserialize_transitions(serialized), data)

    def test_flags(self):
        data = {
            Binding.GO: FIDL_ASSISTED,
            Binding.RUST: MIXED,
        }
        self.assertEqual(to_flags(data), '--go=fidl-assisted --rust=mixed')


if __name__ == '__main__':
    unittest.main()
