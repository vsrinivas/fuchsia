// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_TABLE_HOLDER_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_TABLE_HOLDER_H_

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/fidl/llcpp/fidl_allocator.h>
#include <lib/sysmem-version/sysmem-version.h>

#include <cstddef>
#include <limits>
#include <memory>

#include "macros.h"
#include "table_set.h"

// TableHolder<>
//
// This class holds a table instance and its corresponding fidl::FidlAllocator.
// For the benefit of tables which see ongoing churn, we allow cloning the
// table into a new allocator before the unused space due to churn +
// fidl::FidlAllocator not reclaiming space incrementally causes too much
// memory use.
//
// As required by fidl::FidlAllocator which is an arena allocator, the table is
// always deleted before its allocator is deleted.

class TableHolderBase {
 protected:
  friend class TableSet;

  TableHolderBase(TableHolderBase&& to_move) noexcept;
  TableHolderBase(const TableHolderBase& to_copy) = delete;

  explicit TableHolderBase(TableSet& table_set);
  virtual ~TableHolderBase();

  virtual void clone_to_new_allocator() = 0;

  void CountChurn();

  fidl::AnyAllocator& allocator();

 private:
  TableSet& table_set_;
};

// This GetCloneFunction stuff can go away once llcpp generates clone code.
template <typename Table>
struct GetCloneFunction {};
template <>
struct GetCloneFunction<llcpp::fuchsia::sysmem2::HeapProperties> {
  constexpr static auto value = sysmem::V2CloneHeapProperties;
};
template <>
struct GetCloneFunction<llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers> {
  constexpr static auto value =
      [](fidl::AnyAllocator& allocator,
         const llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers& to_copy)
      -> llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers {
    // struct copy; no allocator involvement
    return to_copy;
  };
};
template <>
struct GetCloneFunction<llcpp::fuchsia::sysmem2::BufferCollectionConstraints> {
  constexpr static auto value = sysmem::V2CloneBufferCollectionConstraints;
};
template <>
struct GetCloneFunction<llcpp::fuchsia::sysmem2::BufferUsage> {
  constexpr static auto value = sysmem::V2CloneBufferUsage;
};
template <>
struct GetCloneFunction<llcpp::fuchsia::sysmem2::BufferCollectionInfo> {
  constexpr static auto value = [](fidl::AnyAllocator& allocator,
                                   const llcpp::fuchsia::sysmem2::BufferCollectionInfo& to_copy)
      -> llcpp::fuchsia::sysmem2::BufferCollectionInfo {
    constexpr uint32_t kAllRights = std::numeric_limits<uint32_t>::max();
    auto result = sysmem::V2CloneBufferCollectionInfo(allocator, to_copy, kAllRights, kAllRights);
    // no reason for the clone to fail other than maybe low memory
    ZX_ASSERT(result.is_ok());
    return result.take_value();
  };
};
// Provide the _v version so we don't need to say ::value when we call GetCloneFunction_v<Table>().
template <typename Table>
constexpr auto GetCloneFunction_v = GetCloneFunction<Table>::value;

template <typename Table>
class TableHolder : public TableHolderBase {
 public:
  // TableHolderBase will take care of registering the new TableHolder* and de-registering the
  // old TableHolder*.
  TableHolder(TableHolder&& to_move) noexcept = default;
  TableHolder(const TableHolder&& to_copy) = delete;

  explicit TableHolder(TableSet& table_set) : TableHolderBase(table_set), table_(allocator()) {}

  explicit TableHolder(TableSet& table_set, Table&& table)
      : TableHolderBase(table_set), table_(std::move(table)) {}

  const Table* operator->() const { return &table_; }

  const Table& operator*() const { return table_; }

  Table& mutate() {
    CountChurn();
    return table_;
  }

 private:
  void clone_to_new_allocator() override {
    Table new_table = GetCloneFunction_v<Table>(allocator(), table_);
    table_ = std::move(new_table);
  }

  friend class TableSet;
  Table table_;
};

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_TABLE_HOLDER_H_
