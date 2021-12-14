# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Utility functions for dealing with class members which are sets and lists.
"""

from typing import Any, Dict, Sequence, Set, Union

__all__ = [
    "set_named_field", "set_if_not_empty", "set_if_named_member_not_empty",
    "set_named_member_if_present"
]


def set_named_field(destination: Any, field: str, value: Any) -> None:
    """Set the field named in `field` to the given `value`, either by using
    destination[field] = value, or destination.field = value, depending on the
    kind of class it is.
    """
    if hasattr(destination, "__setitem__"):
        destination[field] = value
    else:
        setattr(destination, field, value)


def set_if_not_empty(
        destination: Dict[str, Any],
        field: str,
        items: Union[Set[Any], Sequence[Any], Dict[str, Any]],
        sort=False,
        transform=None) -> None:
    """Add the items to the destination container if items is not empty.

    If `sort` is `True`, sort the items.
    """
    if items:
        if transform is not None:
            items = [transform(item) for item in items]
        if sort:
            items = sorted(items)
        set_named_field(destination, field, items)


def set_if_named_member_not_empty(
        destination: Dict[str, Any],
        field: str,
        source: Any,
        sort=False,
        transform=None) -> None:
    """If the member of `source` named `field` is not empty, add it to the destination container,
    with a key of the same name.

    If `sort` is `True`, sort the items.
    """
    items = getattr(source, field)
    set_if_not_empty(destination, field, items, sort, transform)


def set_named_member_if_present(
        destination,
        field: str,
        source: Dict,
        sort=False,
        transform=None,
        transform_items=False):
    """Set a member of `destination` named `field` to the value of the key of the same name
    in the source dict.

    Optionally, it:
    - calls a provided transform func on the source field
    - calls the transform func on each item in the source field
    - sorts the items into a list.
    """
    if field in source:
        items = source[field]
        if transform is not None:
            if transform_items:
                items = [transform(item) for item in items]
            else:
                items = transform(items)
        if sort:
            items = sorted(items)
        set_named_field(destination, field, items)


def intersect_field(item_a, item_b, field: str, result) -> None:
    """Set the named field in `result` if the value for that field is the same
    in items A and B.
    """
    value_a = getattr(item_a, field)
    value_b = getattr(item_b, field)
    if value_a == value_b:
        setattr(result, field, value_a)


def difference_field(item_a, item_b, field: str, result) -> None:
    """Set the named field in `result` to the value from item_a if the value for that field is different
    in items A and B.
    """
    value_a = getattr(item_a, field)
    value_b = getattr(item_b, field)
    if value_a != value_b:
        setattr(result, field, value_a)
