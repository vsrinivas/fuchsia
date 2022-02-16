#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from dataclasses import dataclass, field
import json
from typing import Dict, List, Optional
import unittest

from serialization import (
    instance_to_dict, json_dumps, serialize_dict, serialize_json,
    serialize_fields_as)


class SerializeFieldsTest(unittest.TestCase):
    """Validate that fields of different kinds are properly serialized.
    """

    def test_simple_class(self):

        @dataclass
        class SimpleClass:
            int_field: int
            str_field: str

        instance = SimpleClass(42, "a string")
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': 42,
                'str_field': "a string"
            })

    def test_int_value_0(self):

        @dataclass
        class SimpleClass:
            int_field: int
            str_field: str

        instance = SimpleClass(0, "a string")
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': 0,
                'str_field': "a string"
            })

    def test_optional_field_with_value(self):

        @dataclass
        class SimpleClassWithOptionalField:
            int_field: Optional[int]
            str_field: str

        instance = SimpleClassWithOptionalField(21, "some value")
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': 21,
                'str_field': "some value"
            })

    def test_optional_field_without_value(self):

        @dataclass
        class SimpleClassWithOptionalField:
            int_field: Optional[int]
            str_field: str

        instance = SimpleClassWithOptionalField(None, "some value")
        self.assertEqual(
            instance_to_dict(instance), {'str_field': "some value"})

    def test_serialize_list_fields(self):

        @dataclass
        class SimpleClassWithList:
            int_field: List[int] = field(default_factory=list)
            str_field: str = "foo"

        instance = SimpleClassWithList([1, 2, 3, 4, 5])
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': [1, 2, 3, 4, 5],
                'str_field': "foo"
            })

    def test_serialize_empty_list_fields(self):

        @dataclass
        class SimpleClassWithList:
            int_field: List[int] = field(default_factory=list)
            str_field: str = "foo"

        instance = SimpleClassWithList()
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': [],
                'str_field': "foo"
            })

    def test_serialize_dict_fields(self):

        @dataclass
        class SimpleClassWithDict:
            dict_field: Dict[str, int]

        instance = SimpleClassWithDict({'one': 1, 'two': 2, 'three': 3})
        self.assertEqual(
            instance_to_dict(instance),
            {'dict_field': {
                'one': 1,
                'two': 2,
                'three': 3
            }})

    def test_serialize_fields_as(self):

        @dataclass
        @serialize_fields_as(int_field=str)
        class SimpleClassWithMetdata:
            int_field: int
            str_field: str

        instance = SimpleClassWithMetdata(7, "a string")
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': "7",
                'str_field': "a string",
            })

    def test_serialize_fields_as_with_callable(self):

        def my_serializer(value: int) -> str:
            return f'The value is {value}.'

        @dataclass
        @serialize_fields_as(int_field=my_serializer)
        class SimpleClassWithMetdata:
            int_field: int
            str_field: str

        instance = SimpleClassWithMetdata(7, "a string")
        self.assertEqual(
            instance_to_dict(instance), {
                'int_field': "The value is 7.",
                'str_field': "a string",
            })


class SerializeToDictDecorator(unittest.TestCase):
    """Validate that the `@serialize_to_dict class decorator behaves correctly.

    Note: These function correctly at runtime, but don't interact correctly with
    PyRight, so they don't have proper static analysis and IDE type-checking.
    """

    def test_to_dict_decorator(self):

        @dataclass
        @serialize_dict
        class SimpleClass:
            int_field: int
            str_field: str

        instance = SimpleClass(8, "some value")
        self.assertEqual(
            instance.to_dict(), {
                'int_field': 8,
                'str_field': "some value"
            })

    def test_from_dict_decorator(self):

        @dataclass
        @serialize_dict
        class SimpleClass:
            int_field: int
            str_field: str

        raw = {'int_field': 8, 'str_field': "some value"}
        self.assertEqual(
            SimpleClass.from_dict(raw), SimpleClass(8, "some value"))


class SerializeToJsonDecorator(unittest.TestCase):
    """Validate that the `@serialize_to_json class decorator behaves correctly.

    Note: These function correctly at runtime, but don't interact correctly with
    PyRight, so they don't have proper static analysis and IDE type-checking.
    """

    def test_to_json_decorator(self):

        @dataclass
        @serialize_json
        class SimpleClass:
            int_field: int
            str_field: str

        instance = SimpleClass(8, "some value")
        result = instance.json_dumps(indent=6)
        self.assertEqual(
            result, """{
      "int_field": 8,
      "str_field": "some value"
}""")

    def test_from_json_decorator(self):

        @dataclass
        @serialize_json
        class SimpleClass:
            int_field: int
            str_field: str

        raw = {'int_field': 8, 'str_field': "some value"}
        raw_json = json.dumps(raw)

        self.assertEqual(
            SimpleClass.json_loads(raw_json), SimpleClass(8, "some value"))

    def test_to_json_decorator_with_field_serializer(self):

        def my_serializer(value: int) -> str:
            return f'my value is {value}.'

        @dataclass
        @serialize_fields_as(int_field=my_serializer)
        @serialize_json
        class SimpleClass:
            int_field: int
            str_field: str

        instance = SimpleClass(8, "some value")
        result = instance.json_dumps(indent=6)
        self.assertEqual(
            result, """{
      "int_field": "my value is 8.",
      "str_field": "some value"
}""")
