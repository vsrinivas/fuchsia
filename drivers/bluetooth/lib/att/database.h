// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/att/attribute.h"
#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"

namespace bluetooth {
namespace att {

// This class provides a simple attribute database abstraction. Attributes can
// be populated directly and queried to fulfill ATT protocol requests.
//
// Many Database instances can be created as long as care is taken that the
// referenced handle ranges are distinct. While this class is primarily intended
// to be used as a local ATT server database, it could also be used to represent
// a remote attribute cache.
//
// THREAD-SAFETY:
//
// This class is not thread-safe. The constructor/destructor and all public
// methods must be called on the same thread.
class Database final : public fxl::RefCountedThreadSafe<Database> {
 public:
  // Initializes this database to span the attribute handle range given by
  // |range_start| and |range_end|. This allows the upper layer to segment the
  // handle range into multiple contiguous regions by instantiating multiple
  // Database objects.
  //
  // Note: This is to make it easy for the GATT layer to group service
  // declarations with 16-bit UUIDs and 128-bit UUIDs separately as recommended
  // by the GATT specification (see Vol 3, Part G, 3.1). By default
  inline static fxl::RefPtr<Database> Create(Handle range_start = kHandleMin,
                                             Handle range_end = kHandleMax) {
    return fxl::AdoptRef(new Database(range_start, range_end));
  }

  // Creates a new attribute grouping with the given |type|. The grouping will
  // be initialized to contain |attr_count| attributes (excluding the
  // group declaration attribute) and |value| will be assigned as the group
  // declaration attribute value.
  //
  // Returns a pointer to the new grouping, which can be used to populate
  // attributes. Returns nullptr if the requested grouping could not be
  // created due to insufficient handles.
  //
  // The returned pointer is owned and managed by this Database and should not
  // be retained by the caller. Removing the grouping will invalidate the
  // returned pointer.
  AttributeGrouping* NewGrouping(const common::UUID& group_type,
                                 size_t attr_count,
                                 const common::ByteBuffer& decl_value);

  // Removes the attribute grouping that has the given starting handle. Returns
  // false if no such grouping was found.
  bool RemoveGrouping(Handle start_handle);

  const std::list<AttributeGrouping>& groupings() const { return groupings_; }

  // TODO(armansito): Add lookup functions:
  //   * FindAttribute(Handle);
  //   * ReadByGroupType
  //   * ReadByType
  //   * FindByTypeValue
  //   * FindInformation
  //   * etc

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Database);

  Database(Handle range_start, Handle range_end);
  ~Database() = default;

  Handle range_start_;
  Handle range_end_;

  // The list of groupings is sorted by handle where each grouping maps to a
  // non-overlapping handle range. Successive groupings don't necessarily
  // represent contiguous handle ranges as any grouping can be removed.
  //
  // Note: This uses a std::list because fbl::lower_bound doesn't work with a
  // common::LinkedList (aka fbl::DoublyLinkedList). This is only marginally
  // less space efficient.
  std::list<AttributeGrouping> groupings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Database);
};

}  // namespace att
}  // namespace bluetooth
