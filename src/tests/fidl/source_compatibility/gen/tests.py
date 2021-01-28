# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re
import unittest

from types_ import *
from reverse_test import \
    reverse_steps, reverse_step_nums, reverse_fidl_steps, reverse_src_steps


class TypesTest(unittest.TestCase):

    def test_serialize(self):
        serialized = CompatTest(
            title='Add protocol method',
            fidl={
                'before': FidlDef(source='before.test.fidl', instructions=[]),
                'after': FidlDef(source='after.test.fidl', instructions=[])
            },
            bindings={
                'hlcpp':
                    Steps(
                        starting_fidl='before',
                        starting_src='before.cpp',
                        steps=[
                            SourceStep(
                                step_num=1,
                                source='during.cc',
                                instructions=['do thing']),
                            FidlStep(step_num=2, fidl='after'),
                            SourceStep(
                                step_num=3,
                                source='after.cc',
                                instructions=['voila']),
                        ])
            })
        deserialized = {
            'title': 'Add protocol method',
            'fidl':
                {
                    'before': {
                        'source': 'before.test.fidl',
                        'instructions': []
                    },
                    'after': {
                        'source': 'after.test.fidl',
                        'instructions': []
                    }
                },
            'bindings':
                {
                    'hlcpp':
                        {
                            'starting_fidl':
                                'before',
                            'starting_src':
                                'before.cpp',
                            'steps':
                                [
                                    {
                                        'step_num': 1,
                                        'source': 'during.cc',
                                        'instructions': ['do thing']
                                    },
                                    {
                                        'step_num': 2,
                                        'fidl': 'after',
                                    },
                                    {
                                        'step_num': 3,
                                        'source': 'after.cc',
                                        'instructions': ['voila']
                                    },
                                ]
                        }
                }
        }
        self.assertEqual(serialized.todict(), deserialized)
        self.assertEqual(CompatTest.fromdict(deserialized), serialized)


class ReverseStepNumTest(unittest.TestCase):

    def test_symmetric(self):
        # odd number of steps and odd number of total steps means the steps
        # are symmetric so there is no change in step numbers:
        #
        # init | 1 | 2 | 3 | 4 | 5
        # -------------------------
        #   A  | B |   | C |   | D
        #
        # becomes
        #
        # init | 1 | 2 | 3 | 4 | 5
        # -------------------------
        #   D  | C |   | B |   | A
        self.assertEqual(reverse_step_nums([0, 1, 3, 5], 5), [0, 1, 3, 5])

    def test_asymmetric(self):
        # even number of steps and even number of total steps means the step
        # numbers change:
        #
        # init | 1 | 2 | 3 | 4
        # ---------------------
        #   A  | B |   | C |
        #
        # becomes
        #
        # init | 1 | 2 | 3 | 4
        # ---------------------
        #   C  |   | B |   | A
        self.assertEqual(reverse_step_nums([0, 1, 3], 4), [0, 2, 4])


class ReverseStepsTest(unittest.TestCase):
    maxDiff = None

    def test_basic(self):
        self.assertEqual(
            reverse_steps(['step_00_foo', 'step_01_bar', 'step_03_baz'], 3),
            ['step_00_foo', 'step_01_bar', 'step_03_baz'])

    def test_with_parent_paths_and_ext(self):
        self.assertEqual(
            reverse_steps(['foo/step_00_a.abc', 'bar/step_01_b.xyz'], 2),
            ['bar/step_00_a.abc', 'foo/step_02_b.xyz'])

    def test_fidl(self):
        defs = {
            'step_00_before':
                FidlDef(
                    source='fidl/step_00_before.test.fidl', instructions=[]),
            'step_02_during':
                FidlDef(
                    source='fidl/step_02_during.test.fidl',
                    instructions=['add/remove method with transitional']),
            'step_04_after':
                FidlDef(
                    source='fidl/step_04_after.test.fidl',
                    instructions=['add/remove transitional']),
        }
        reversed_defs, old_to_new = reverse_fidl_steps(defs, max_step_num=4)

        self.assertEqual(
            reversed_defs, {
                'step_00_before':
                    FidlDef(
                        source='fidl/step_00_before.test.fidl',
                        instructions=[]),
                'step_01_during':
                    FidlDef(
                        source='fidl/step_01_during.test.fidl',
                        instructions=['add/remove transitional']),
                'step_03_after':
                    FidlDef(
                        source='fidl/step_03_after.test.fidl',
                        instructions=['add/remove method with transitional']),
            })

        self.assertEqual(
            old_to_new, {
                'step_00_before': 'step_03_after',
                'step_02_during': 'step_01_during',
                'step_04_after': 'step_00_before'
            })

    def test_src_fidl_assisted(self):
        fidl_mapping = {
            'step_00_before': 'step_03_after',
            'step_02_during': 'step_01_during',
            'step_04_after': 'step_00_before'
        }
        transition = Steps(
            starting_fidl='step_00_before',
            starting_src='hlcpp/step_00_before.cc',
            steps=[
                FidlStep(step_num=2, fidl='step_02_during'),
                SourceStep(
                    step_num=3,
                    source='hlcpp/step_03_after.cc',
                    instructions=[]),
                FidlStep(step_num=4, fidl='step_04_after'),
            ])
        reversed_transition, old_to_new = reverse_src_steps(
            transition, fidl_mapping, max_step_num=4)

        self.assertEqual(
            reversed_transition,
            Steps(
                starting_fidl='step_00_before',
                starting_src='hlcpp/step_00_before.cc',
                steps=[
                    FidlStep(step_num=1, fidl='step_01_during'),
                    SourceStep(
                        step_num=2,
                        source='hlcpp/step_02_after.cc',
                        instructions=[]),
                    FidlStep(step_num=3, fidl='step_03_after'),
                ]))

        self.assertEqual(
            old_to_new, {
                'hlcpp/step_00_before.cc': 'hlcpp/step_02_after.cc',
                'hlcpp/step_03_after.cc': 'hlcpp/step_00_before.cc',
            })

    def test_src_src_assisted(self):
        fidl_mapping = {
            'step_00_before': 'step_03_after',
            'step_02_during': 'step_01_during',
            'step_04_after': 'step_00_before'
        }
        transition = Steps(
            starting_fidl='step_00_before',
            starting_src='rust/step_00_before.rs',
            steps=[
                SourceStep(
                    step_num=1,
                    source='rust/step_01_during.rs',
                    instructions=['a']),
                FidlStep(step_num=2, fidl='step_02_during'),
                SourceStep(
                    step_num=3,
                    source='rust/step_03_after.rs',
                    instructions=['b']),
                FidlStep(step_num=4, fidl='step_04_after'),
            ])
        reversed_transition, old_to_new = reverse_src_steps(
            transition, fidl_mapping, max_step_num=4)

        self.assertEqual(
            reversed_transition,
            Steps(
                starting_fidl='step_00_before',
                starting_src='rust/step_00_before.rs',
                steps=[
                    FidlStep(step_num=1, fidl='step_01_during'),
                    SourceStep(
                        step_num=2,
                        source='rust/step_02_during.rs',
                        instructions=['b']),
                    FidlStep(step_num=3, fidl='step_03_after'),
                    SourceStep(
                        step_num=4,
                        source='rust/step_04_after.rs',
                        instructions=['a']),
                ]))

        self.assertEqual(
            old_to_new, {
                'rust/step_00_before.rs': 'rust/step_04_after.rs',
                'rust/step_01_during.rs': 'rust/step_02_during.rs',
                'rust/step_03_after.rs': 'rust/step_00_before.rs',
            })


if __name__ == '__main__':
    unittest.main()
