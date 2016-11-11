# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import module as mojom

# This module provides a mechanism for determining the packed order and offsets
# of a mojom.Struct.
#
# ps = pack.PackedStruct(struct)
# ps.packed_fields will access a list of PackedField objects, each of which
# will have an offset, a size and a bit (for mojom.BOOLs).

# Size of struct header in bytes: num_bytes [4B] + version [4B].
HEADER_SIZE = 8

class PackedField(object):
  kind_to_size = {
    mojom.BOOL:                  1,
    mojom.INT8:                  1,
    mojom.UINT8:                 1,
    mojom.INT16:                 2,
    mojom.UINT16:                2,
    mojom.INT32:                 4,
    mojom.UINT32:                4,
    mojom.FLOAT:                 4,
    mojom.HANDLE:                4,
    mojom.CHANNEL:               4,
    mojom.VMO:                   4,
    mojom.PROCESS:               4,
    mojom.THREAD:                4,
    mojom.EVENT:                 4,
    mojom.PORT:                  4,
    mojom.DCPIPE:                4,
    mojom.DPPIPE:                4,
    mojom.NULLABLE_HANDLE:       4,
    mojom.NULLABLE_CHANNEL:      4,
    mojom.NULLABLE_VMO:          4,
    mojom.NULLABLE_PROCESS:      4,
    mojom.NULLABLE_THREAD:       4,
    mojom.NULLABLE_EVENT:        4,
    mojom.NULLABLE_PORT:         4,
    mojom.NULLABLE_DCPIPE:       4,
    mojom.NULLABLE_DPPIPE:       4,
    mojom.INT64:                 8,
    mojom.UINT64:                8,
    mojom.DOUBLE:                8,
    mojom.STRING:                8,
    mojom.NULLABLE_STRING:       8
  }

  @classmethod
  def GetSizeForKind(cls, kind):
    if isinstance(kind, (mojom.Array, mojom.Map, mojom.Struct,
                         mojom.Interface)):
      return 8
    if isinstance(kind, mojom.Union):
      return 16
    if isinstance(kind, mojom.InterfaceRequest):
      kind = mojom.CHANNEL
    if isinstance(kind, mojom.Enum):
      # TODO(mpcomplete): what about big enums?
      return cls.kind_to_size[mojom.INT32]
    if not kind in cls.kind_to_size:
      raise Exception("Invalid kind: %s" % kind.spec)
    return cls.kind_to_size[kind]

  def __init__(self, field):
    """
    Args:
      field: the original field.
    """
    self.field = field
    self.index = field.declaration_order
    self.ordinal = field.computed_ordinal
    self.size = self.GetSizeForKind(field.kind)
    self.offset = field.computed_offset
    self.bit = field.computed_bit
    self.min_version = field.computed_min_version


def GetPad(offset, alignment):
  """Returns the pad necessary to reserve space so that |offset + pad| equals to
  some multiple of |alignment|."""
  return (alignment - (offset % alignment)) % alignment


class PackedStruct(object):
  def __init__(self, struct):
    self.struct = struct

    # |packed_fields_in_ordinal_order| contains all the fields,
    # in ordinal order.
    self.packed_fields_in_ordinal_order = [PackedField(field)
        for field in struct.fields_in_ordinal_order]

    # |packed_fields| refers to the same fields as
    # |packed_fields_in_ordinal_order|, but in increasing offset order.
    self.packed_fields = [field for field in
        self.packed_fields_in_ordinal_order]
    self.packed_fields.sort(key=lambda f: (f.offset, f.bit))

class ByteInfo(object):
  def __init__(self):
    self.is_padding = False
    self.packed_fields = []


def GetByteLayout(packed_struct):
  total_payload_size = packed_struct.struct.versions[-1].num_bytes - HEADER_SIZE
  bytes = [ByteInfo() for i in xrange(total_payload_size)]

  limit_of_previous_field = 0
  for packed_field in packed_struct.packed_fields:
    for i in xrange(limit_of_previous_field, packed_field.offset):
      bytes[i].is_padding = True
    bytes[packed_field.offset].packed_fields.append(packed_field)
    limit_of_previous_field = packed_field.offset + packed_field.size

  for i in xrange(limit_of_previous_field, len(bytes)):
    bytes[i].is_padding = True

  for byte in bytes:
    # A given byte cannot both be padding and have a fields packed into it.
    assert not (byte.is_padding and byte.packed_fields)

  return bytes


class VersionInfo(object):
  def __init__(self, version, num_fields, num_bytes):
    self.version = version
    self.num_fields = num_fields
    self.num_bytes = num_bytes

