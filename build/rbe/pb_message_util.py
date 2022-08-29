#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Library for converting protobuf messages to dictionaries for BQ.

Similar to json_format.MessageToDict, but handles maps properly.
This works on any proto Message subtype.
"""

from api.proxy import log_pb2
from google.protobuf import json_format
from google.protobuf import descriptor
from google.protobuf import message
from google.protobuf import timestamp_pb2
from google.protobuf.internal import containers as proto_containers
from typing import Any, Callable, Dict, Sequence, Tuple


def _dict_to_key_values(d: Dict[str, Any]) -> Sequence[Dict[str, Any]]:
    return [
        {
            "key": k,
            "value": _value_to_bq_dict(v)
        } for k, v in sorted(d.items())
    ]


def _value_to_bq_dict(val):
    """Converts data to json_format dictionary form."""
    if isinstance(val, message.Message):
        return proto_message_to_bq_dict(val)
    else:
        return val


def _convert_repeated_bq_value(fd: descriptor.FieldDescriptor,
                               value: Any) -> Sequence[Any]:
    # proto maps appear as Message types, but are implemented as special classes
    # instead of dictionaries (but still have a dictionary interface).
    if fd.type == descriptor.FieldDescriptor.TYPE_MESSAGE:
        if isinstance(value, proto_containers.MessageMap) or isinstance(
                value, proto_containers.ScalarMap):
            return _dict_to_key_values(value)
    return [_convert_scalar_bq_value(fd, v) for v in value]


def _convert_scalar_bq_value(fd: descriptor.FieldDescriptor, value: Any) -> Any:
    if fd.type == descriptor.FieldDescriptor.TYPE_MESSAGE:
        # Timestamps should be formatted like '2022-08-24T23:40:55.042750824Z',
        # instead of (seconds, nanos).
        if isinstance(value, timestamp_pb2.Timestamp):
            return json_format.MessageToDict(value)
        else:
            return proto_message_to_bq_dict(value)
    elif fd.type == descriptor.FieldDescriptor.TYPE_ENUM:
        # Want enum names, not numbers.
        return fd.enum_type.values_by_number[value].name
    else:
        return _value_to_bq_dict(value)


def _convert_bq_value(fd: descriptor.FieldDescriptor, value: Any) -> Any:
    if fd.label == descriptor.FieldDescriptor.LABEL_REPEATED:
        return _convert_repeated_bq_value(fd, value)
    else:
        return _convert_scalar_bq_value(fd, value)


def proto_message_to_bq_dict(msg: message.Message) -> Dict[str, Any]:
    """Works like json_format.MessageToDict, but handles maps properly.

    Builds up a dictionary recursively.

    Args:
      msg: Any protobuf message object.

    Returns:
      json-structured dictionary.
    """
    result = {
        field_descriptor.name: _convert_bq_value(field_descriptor, value)
        for field_descriptor, value in msg.ListFields()
    }
    return result
