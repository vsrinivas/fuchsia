# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Serialization Methods for Classes.

This module is inspired by the `serde` crate in Rust, and the dataclasses python
module.

It provides a type-aware mechanism for serializing and deserializing classes
to/from dictionaries of string key:values, and then from those to/from JSON.

The module relies on PEP-526 type annotations in order to function.  If members
of the class are missing type annotations, it will not function correctly.

Example:

    from dataclasses import dataclass, field
    from typing import Dict, List, Optional
    from serialization import instance_to_dict, json_dumps, json_loads

    @dataclass
    class Child:
        name: str
        height: Optional[int] = None
        interests: List[str] = field(default_factory=list)

    @dataclass
    class Parent:
        name: str
        children: List[Child] = field(default_factory=list)

    parent = Parent("Some Person")
    parent.children.append(Child("A Child", 130, ["reading", "cats"]))

    print(parent)
    print("\n")
    print(instance_to_dict(parent))

will result in:

    Parent(name='Some Person', children=[Child(name='A Child', height=130,
    interests=['reading', 'cats'])])

    {'name': 'Some Person', 'children': [{'name': 'A Child', 'height': 130,
    'interests': ['reading', 'cats']}]}

and:

    print(json_dumps(parent, indent=2))

will yield:

    {
      "name": "Some Person",
      "children": [
        {
          "name": "A Child",
          "height": 130,
          "interests": [
            "reading",
            "cats"
          ]
        }
      ]
    }

Parsing is similarly straightforward:

    json_string = '{"name":"First Last","children":[\
        {"name":"a child", "interests":["toys", "games"]}]}
    parent = json_loads(Parent, json_string)
    print(parent)

yields:

    Parent(name='First Last', children=[Child(name='a child', height=None,
    interests=['toys', 'games'])])

Which can then be used as the object that it is:

    for line in ['{} likes {}'.format(child.name, " and ".join(child.interests))
                for child in parent.children]:
        print(line)

to print:

    a child likes toys and games

"""

import functools
import inspect
import json

from typing import (
    Any, Callable, Dict, List, Optional, Type, TypeVar, Union, get_type_hints)
import typing

__all__ = [
    'instance_from_dict', 'instance_to_dict', 'json_dump', 'json_dumps',
    'json_load', 'json_loads', 'serialize_dict', 'serialize_json',
    'serialize_fields_as'
]

# The placeholder for the Class of the object that's being serialized or
# deserialized.
C = TypeVar("C")


def instance_from_dict(cls: Type[C], a_dict: Dict[str, Any]) -> C:
    """Instantiate an object from a dictionary of its field values.

    The default strategy is to instantiate a default instance of the class, and
    then set any fields whose values are found in the dictionary.

    This relies on type annotations that specify the class of each field and
    each init param.  All must match names in the entry dictionary.
    """
    init_param_types = get_fn_param_types(cls, "__init__")
    init_param_values = get_named_values_from(a_dict, init_param_types)
    instance = cls(**init_param_values)

    field_types = typing.get_type_hints(cls)

    # Strip the fields that were provided via the constructor.
    fields_to_read = {
        name: field_types[name]
        for name in field_types.keys()
        if name not in init_param_types
    }

    # If any fields weren't set via the constructor, set those attributes on the
    # class directly.  For classes that use @dataclass, this is likely to be
    # empty.
    for field_name, value in get_named_values_from(a_dict, fields_to_read):
        setattr(instance, field_name, value)

    return instance


def get_fn_param_types(cls: Type[Any], fn_name: str) -> Dict[str, Type[Any]]:
    """Get the names and types of the parameters of the fn with the given name
    for the given class.

    Strips out 'self', and only returns the other params.
    """
    fn_sig = inspect.signature(cls.__dict__[fn_name])
    return {
        name: parameter.annotation
        for name, parameter in fn_sig.parameters.items()
        if name != 'self'
    }


def has_field_types(cls: Type[Any]) -> bool:
    """Returns True if the class in question has member field type annotations.

    This is akin to [`typing.get_type_hints()`], except that this doesn't raise
    any errors on types that don't support annotations at all (like ['Union']).
    """
    return "__annotations__" in cls.__dict__


def get_named_values_from(
        entries: Dict[str, Any],
        names_and_types: Dict[str, Type[Any]]) -> Dict[str, Any]:
    """Take a Dict of name:value, and a dict of name:types, and return a dict of
    name to instantiated-type-for-value.

    Example::
        >>> entry = { "my_int": "42", "some_vals": [ "1", "2", "3" ]}
        >>> names_and_types = { "my_int":"int", "some_vals": "List[int]" }
        >>> get_named_values_from(entry, names_and_types)
        {"my_int":42, "some_vals":[1, 2, 3]}

    """
    values = {}
    for name, cls in names_and_types.items():
        if name in entries:
            values[name] = parse_dict_value_into(cls, entries[name])
        else:
            # Is the field optional?
            if typing.get_origin(cls) is Union and type(
                    None) in typing.get_args(cls):
                # Yes, so just set it to None.
                values[name] = None
            else:
                raise KeyError(
                    "param '{}' not found in dict:\n{}".format(
                        name, entries.keys()))

    return values


def parse_dict_value_into(cls: Type[Union[Dict, List, C]],
                          value: Any) -> Union[Dict, List, C]:
    """For a class, attempt to parse it from the value.
    """
    if typing.get_origin(cls) is dict:
        # dict values need to have a type
        type_args = typing.get_args(cls)
        if type_args:
            dict_key_type, dict_value_type = type_args
        else:
            raise TypeError(f"Cannot deserialize untyped Dicts: {cls}")

        # value also has to be a dict
        if type(value) is not dict:
            raise TypeError(
                f"cannot parse {cls} from a non-dict value of type: {type(value)}"
            )

        result = dict()
        for key, dict_value in value.items():
            key = parse_dict_value_into(dict_key_type, key)
            result[key] = parse_dict_value_into(dict_value_type, dict_value)
        return result

    elif typing.get_origin(cls) is list:
        # List items need to have a type
        list_item_type = typing.get_args(cls)[0]
        # value also has to be a list
        if type(value) is not list:
            raise TypeError(f'cannot parse {cls} from a non-list value')

        return [parse_dict_value_into(list_item_type, item) for item in value]

    elif has_field_types(cls):
        # Create an object from this value
        return instance_from_dict(cls, value)

    elif typing.get_origin(cls) is Union:
        # Unions are special, because we don't know what value we can make, so
        # just try them all, in order, until we get one that works.
        errors = []
        for arg in typing.get_args(cls):
            try:
                return parse_dict_value_into(arg, value)
            except KeyError as ke:
                errors.append(ke)
            except TypeError as te:
                errors.append(te)
            except ValueError as ve:
                errors.append(ve)
        raise TypeError(
            f"Unable to create an instance of {cls}, from {value}: {errors}")

    else:
        # It's probably a simple type, so directly instantiate it
        return cls(value)


def make_dict_value_for(obj: Any) -> Union[Dict, List, str, int]:
    """Create the value to put into a dictionary for the given object."""
    if isinstance(obj, dict):
        # Dicts are special, and need to be treated individually.
        result = {}
        for key, value in obj.items():
            # Recurse for each value in the dict.
            result[str(key)] = make_dict_value_for(value)
        return result

    elif isinstance(obj, list):
        # Lists are also special
        return [make_dict_value_for(value) for value in obj]

    elif has_field_types(type(obj)):
        # It's something else, and it has field type annotations, so let's use
        # those to get a dictionary.
        return instance_to_dict(obj)

    else:
        # It doesn't support type hints, so just use it as-is, and hope for the
        # best.
        return obj


def instance_to_dict(instance: Any) -> Dict[str, Any]:
    """Convert the object to a dictionary of its fields, ready for serialization
    into JSON.

    This supports classes that use PEP-526 type annotations, and is meant to
    work especially well with classes that use [`@dataclass`], such as the
    following example::

        from dataclass import dataclass
        from typing import Optional
        @dataclass
        class FooClass:
            some_field: int
            some_other_field: Optional[str]
     """
    field_types = typing.get_type_hints(instance)
    result = {}
    for name in field_types.keys():
        # First get the value of each field.
        value = getattr(instance, name)
        if value is not None:
            # If a serializer fn was added via metadata, use that. Otherwise use
            # the "default" handler
            metadata: Optional[Dict] = getattr(
                instance.__class__, '__SERIALIZE_AS__', None)
            if metadata and name in metadata:
                serializer: Callable = metadata.get(name)
            else:
                serializer = make_dict_value_for
            result[name] = serializer(value)

    return result


def json_loads(cls: Type[C], s: str) -> C:
    """Deserialize an instance of type 'cls' from JSON in the string 's'.

    This supports classes that use PEP-526 type annotations, and is meant to
    work especially well with classes that use [`@dataclass`], such as the
    following example::

        from dataclass import dataclass
        from typing import Optional
        @dataclass
        class FooClass:
            some_field: int
            some_other_field: Optional[str]
     """
    return instance_from_dict(cls, json.loads(s))


def json_load(cls: Type[C], fp) -> C:
    """Deserialize an instance of type 'cls' from JSON read from a read()-
    supporting object.

    This supports classes that use PEP-526 type annotations, and is meant to
    work especially well with classes that use [`@dataclass`], such as the
    following example::

        from dataclass import dataclass
        from typing import Optional
        @dataclass
        class FooClass:
            some_field: int
            some_other_field: Optional[str]
    """
    return instance_from_dict(cls, json.load(fp))


def json_dump(instance: Any, fp, **kwargs) -> None:
    """Serialize an object into json written to a write()-supporting object.

    This supports classes that use PEP-526 type annotations, and is meant to
    work especially well with classes that use [`@dataclass`], such as the
    following example::

        from dataclass import dataclass
        from typing import Optional
        @dataclass
        class FooClass:
            some_field: int
            some_other_field: Optional[str]
    """
    json.dump(instance_to_dict(instance), fp, **kwargs)


def json_dumps(instance: Any, **kwargs) -> str:
    """Serialize an object into json written to a string.

    This supports classes that use PEP-526 type annotations, and is meant to
    work especially well with classes that use [`@dataclass`], such as the
    following example::

        from dataclass import dataclass
        from typing import Optional
        @dataclass
        class FooClass:
            some_field: int
            some_other_field: Optional[str]
    """
    return json.dumps(instance_to_dict(instance), **kwargs)


C = TypeVar('C')


def _bind_class_fn(cls: Type[C], fn: Callable, name: str = None):
    """Creates a class-fn for a class by binding the passed-in class as the first
    param of the passed-in function, and then adding it as a callable attribute
    to the class.
    """
    if name is None:
        name = fn.__name__
    setattr(cls, name, functools.partial(fn, cls))


def _bind_instance_fn(cls: Type[C], fn: Callable, name: str = None):
    """Creates an instance-fn for a class by adding it as a callable attribute
    of the class.
    """
    if name is None:
        name = fn.__name__
    setattr(cls, name, fn)


def serialize_dict(cls: Type[C]) -> Type[C]:
    """A decorator that adds dictionary-based serialization and deserialization
    fns to the class, which operate using the type annotations for the class's
    members and the params of the __init__() fn.

    Examines PEP 526 __annotations__ to determine how to serialize/deserialize
    the given class.

    Example:
    ```
    @dataclass
    @serialize_dict
    class MyClass:
        some_field: int
        another_field: string
    ```

    This decorator adds the following functions to the class definition:
    ```
    @classfunction
    def from_dict(cls: Type[Self], value: Dict) -> Self:
      return serialization.instance_from_dict(cls, value)

    def to_dict(self) -> Dict:
      return serialization.instance_to_dict(self)
    ```
    """

    def wrap(cls: Type[C]) -> Type[C]:
        _bind_class_fn(cls, instance_from_dict, "from_dict")
        _bind_instance_fn(cls, instance_to_dict, "to_dict")
        return cls

    return wrap(cls)


def serialize_json(cls: Type[C]) -> Type[C]:
    """A decorator that adds JSON serialization and deserialization fns to the
    class, which follow the [`json.load()`], [`json.dumps()`], etc. functions.
    They operate using the type annotations for the class's members and the
    params of the __init__() fn.

    Examines PEP 526 __annotations__ to determine how to serialize/deserialize
    the given class.

    Example:
    ```
    @dataclass
    @serialize_json
    class MyClass:
        some_field: int
        another_field: string
    ```

    This decorator adds the following functions to the class definition:
    ```
    @classfunction
    def json_loads(cls: Type[Self], value: str) -> Self:
      return serialization.json_loads(cls, value)

    @classfunction
    def json_load(cls: Type[Self], fp: SupportsRead) -> Self:
      return serialization.json_load(cls, fp)

    def json_dumps(self, **kwargs) -> str:
      return serialization.json_dumps(self, **kwargs)

    def json_dump(self, fp: SupportsWrite, **kwargs) -> str:
      return serialization.json_dump(self, fp, **kwargs)
    ```
    """

    def wrap(cls: Type[C]) -> Type[C]:
        _bind_class_fn(cls, json_load)
        _bind_class_fn(cls, json_loads)
        _bind_instance_fn(cls, json_dump)
        _bind_instance_fn(cls, json_dumps)
        return cls

    return wrap(cls)


def _process_metadata(cls: Type[C], **kwargs) -> Type[C]:
    for name in kwargs.keys():
        if not hasattr(cls, name):
            annotations = get_type_hints(cls)
            if name not in annotations:
                raise ValueError(f'{cls} does not have a field named: {name}:')
    if kwargs:
        setattr(cls, "__SERIALIZE_AS__", kwargs)
    return cls


def serialize_fields_as(**kwargs) -> Callable[[Type[C]], Type[C]]:
    """Adds serialization metadata to the class, which is used to augment the
    PEP 526 __annotations__ to determine how to serialize the fields of the
    given class.

    Each is provided as a `fieldname=class` pair, or a `fieldname=fn` pair,
    which is called when serializing the field with that name.

    Example:
    ```
    def some_func(value: str) -> str:
        return f'serialized {value}.'

    @dataclass
    @serialize_json
    @serialize_fields_as(my_int_field=str,my_other_field=some_func)
    class MyClass:
        my_int_field: int
        my_other_field: str

    instance = MyClass(45, "hello")
    assertEqual(
        instance.json_dumps(),
        '{"my_int_field":"45","my_other_field":"serialized hello"}'
    )
    ```
    """

    def wrap(cls: Type[C]) -> Type[C]:
        return _process_metadata(cls, **kwargs)

    return wrap
