// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics.h"

#include <iostream>

#include "lib/inspect/cpp/inspector.h"
#include "lib/inspect/cpp/vmo/types.h"

namespace ftl {
namespace {

enum class BlockOperationType {
  kFlush,
  kTrim,
  kRead,
  kWrite,
};

enum class NandOperationType {
  kAll,
  kPageRead,
  kPageWrite,
  kBlockErase,
};

std::string GetName(BlockOperationType block_operation) {
  switch (block_operation) {
    case BlockOperationType::kFlush:
      return "block.flush";
    case BlockOperationType::kRead:
      return "block.read";
    case BlockOperationType::kTrim:
      return "block.trim";
    case BlockOperationType::kWrite:
      return "block.write";
  };
}

std::string GetName(NandOperationType nand_operation) {
  switch (nand_operation) {
    case NandOperationType::kPageRead:
      return "page_read";
    case NandOperationType::kPageWrite:
      return "page_write";
    case NandOperationType::kBlockErase:
      return "block_erase";
    case NandOperationType::kAll:
      return "nand_operation";
  }
}

BlockOperationType kAllBlockOps[] = {BlockOperationType::kRead, BlockOperationType::kWrite,
                                     BlockOperationType::kTrim, BlockOperationType::kFlush};

NandOperationType kAllNandOps[] = {NandOperationType::kBlockErase, NandOperationType::kPageRead,
                                   NandOperationType::kPageWrite, NandOperationType::kAll};

std::string GetCounterPropertyName(BlockOperationType operation_type) {
  auto name = GetName(operation_type);
  return name + ".count";
}

std::string GetCounterPropertyName(BlockOperationType operation_type,
                                   NandOperationType nand_operation) {
  auto name = GetName(operation_type);
  auto nested_name = GetName(nand_operation);

  return name + ".issued_" + nested_name + ".count";
}

std::string GetRatePropertyName(BlockOperationType operation_type,
                                NandOperationType nand_operation) {
  auto name = GetName(operation_type);
  auto nested_name = GetName(nand_operation);

  return name + ".issued_" + nested_name + ".average_rate";
}

BlockOperationProperties MakePropertyForBlockOperation(inspect::Node& root,
                                                       BlockOperationType block_operation) {
  auto count = root.CreateUint(GetCounterPropertyName(block_operation), 0);
  auto all_nand = NestedNandOperationProperties(
      root.CreateUint(GetCounterPropertyName(block_operation, NandOperationType::kAll), 0),
      root.CreateDouble(GetRatePropertyName(block_operation, NandOperationType::kAll), 0));
  auto nand_page_read = NestedNandOperationProperties(
      root.CreateUint(GetCounterPropertyName(block_operation, NandOperationType::kPageRead), 0),
      root.CreateDouble(GetRatePropertyName(block_operation, NandOperationType::kPageRead), 0));
  auto nand_page_write = NestedNandOperationProperties(
      root.CreateUint(GetCounterPropertyName(block_operation, NandOperationType::kPageWrite), 0),
      root.CreateDouble(GetRatePropertyName(block_operation, NandOperationType::kPageWrite), 0));
  auto nand_block_erase = NestedNandOperationProperties(
      root.CreateUint(GetCounterPropertyName(block_operation, NandOperationType::kBlockErase), 0),
      root.CreateDouble(GetRatePropertyName(block_operation, NandOperationType::kBlockErase), 0));

  return BlockOperationProperties{.count = std::move(count),
                                  .all = std::move(all_nand),
                                  .page_read = std::move(nand_page_read),
                                  .page_write = std::move(nand_page_write),
                                  .block_erase = std::move(nand_block_erase)};
}

}  // namespace

template <>
std::vector<std::string> Metrics::GetPropertyNames<inspect::UintProperty>() {
  std::vector<std::string> property_names;
  for (auto block_op : kAllBlockOps) {
    property_names.push_back(GetCounterPropertyName(block_op));
    for (auto nand_op : kAllNandOps) {
      property_names.push_back(GetCounterPropertyName(block_op, nand_op));
    }
  }
  property_names.push_back("nand.erase_block.max_wear");
  property_names.push_back("nand.initial_bad_blocks");
  property_names.push_back("nand.running_bad_blocks");
  return property_names;
}

template <>
std::vector<std::string> Metrics::GetPropertyNames<inspect::DoubleProperty>() {
  std::vector<std::string> property_names;
  for (auto block_op : kAllBlockOps) {
    for (auto nand_op : kAllNandOps) {
      property_names.push_back(GetRatePropertyName(block_op, nand_op));
    }
  }
  return property_names;
}

Metrics::Metrics()
    : inspector_(),
      root_(inspector_.GetRoot().CreateChild("ftl")),
      read_(MakePropertyForBlockOperation(root_, BlockOperationType::kRead)),
      write_(MakePropertyForBlockOperation(root_, BlockOperationType::kWrite)),
      flush_(MakePropertyForBlockOperation(root_, BlockOperationType::kFlush)),
      trim_(MakePropertyForBlockOperation(root_, BlockOperationType::kTrim)) {
  max_wear_ = root_.CreateUint("nand.erase_block.max_wear", 0);
  initial_bad_blocks_ = root_.CreateUint("nand.initial_bad_blocks", 0);
  running_bad_blocks_ = root_.CreateUint("nand.running_bad_blocks", 0);
}

}  // namespace ftl
