# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Utility functions for dealing with class members which are sets and lists.
"""

__all__ = ["difference_field", "intersect_field"]


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
