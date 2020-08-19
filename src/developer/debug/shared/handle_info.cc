// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/handle_info.h"

namespace debug_ipc {

std::string HandleTypeToString(uint32_t handle_type) {
  // Don't use Zircon headers from here, so need to hardcode the values.
  switch (handle_type) {
    case 0u:
      return "ZX_OBJ_TYPE_NONE";
    case 1u:
      return "ZX_OBJ_TYPE_PROCESS";
    case 2u:
      return "ZX_OBJ_TYPE_THREAD";
    case 3u:
      return "ZX_OBJ_TYPE_VMO";
    case 4u:
      return "ZX_OBJ_TYPE_CHANNEL";
    case 5u:
      return "ZX_OBJ_TYPE_EVENT";
    case 6u:
      return "ZX_OBJ_TYPE_PORT";
    case 9u:
      return "ZX_OBJ_TYPE_INTERRUPT";
    case 11u:
      return "ZX_OBJ_TYPE_PCI_DEVICE";
    case 12u:
      return "ZX_OBJ_TYPE_LOG";
    case 14u:
      return "ZX_OBJ_TYPE_SOCKET";
    case 15u:
      return "ZX_OBJ_TYPE_RESOURCE";
    case 16u:
      return "ZX_OBJ_TYPE_EVENTPAIR";
    case 17u:
      return "ZX_OBJ_TYPE_JOB";
    case 18u:
      return "ZX_OBJ_TYPE_VMAR";
    case 19u:
      return "ZX_OBJ_TYPE_FIFO";
    case 20u:
      return "ZX_OBJ_TYPE_GUEST";
    case 21u:
      return "ZX_OBJ_TYPE_VCPU";
    case 22u:
      return "ZX_OBJ_TYPE_TIMER";
    case 23u:
      return "ZX_OBJ_TYPE_IOMMU";
    case 24u:
      return "ZX_OBJ_TYPE_BTI";
    case 25u:
      return "ZX_OBJ_TYPE_PROFILE";
    case 26u:
      return "ZX_OBJ_TYPE_PMT";
    case 27u:
      return "ZX_OBJ_TYPE_SUSPEND_TOKEN";
    case 28u:
      return "ZX_OBJ_TYPE_PAGER";
    case 29u:
      return "ZX_OBJ_TYPE_EXCEPTION";
    case 30u:
      return "ZX_OBJ_TYPE_CLOCK";
    case 31u:
      return "ZX_OBJ_TYPE_STREAM";
    case 32u:
      return "ZX_OBJ_TYPE_MSI_ALLOCATION";
    case 33u:
      return "ZX_OBJ_TYPE_MSI_INTERRUPT";
    default:
      return "<unknown (" + std::to_string(handle_type) + ")>";
  }
}

std::vector<std::string> HandleRightsToStrings(uint32_t handle_rights) {
  std::vector<std::string> result;
  if (handle_rights == 0) {
    result.emplace_back("ZX_RIGHT_NONE");
    return result;
  }

  struct RightMapping {
    uint32_t bit_value;
    const char* name;
  };
  static const RightMapping kRightMapping[] = {
      {1u << 0, "ZX_RIGHT_DUPLICATE"},      {1u << 1, "ZX_RIGHT_TRANSFER"},
      {1u << 2, "ZX_RIGHT_READ"},           {1u << 3, "ZX_RIGHT_WRITE"},
      {1u << 4, "ZX_RIGHT_EXECUTE"},        {1u << 5, "ZX_RIGHT_MAP"},
      {1u << 6, "ZX_RIGHT_GET_PROPERTY"},   {1u << 7, "ZX_RIGHT_SET_PROPERTY"},
      {1u << 8, "ZX_RIGHT_ENUMERATE"},      {1u << 9, "ZX_RIGHT_DESTROY"},
      {1u << 10, "ZX_RIGHT_SET_POLICY"},    {1u << 11, "ZX_RIGHT_GET_POLICY"},
      {1u << 12, "ZX_RIGHT_SIGNAL"},        {1u << 13, "ZX_RIGHT_SIGNAL_PEER"},
      {1u << 14, "ZX_RIGHT_WAIT"},          {1u << 15, "ZX_RIGHT_INSPECT"},
      {1u << 16, "ZX_RIGHT_MANAGE_JOB"},    {1u << 17, "ZX_RIGHT_MANAGE_PROCESS"},
      {1u << 18, "ZX_RIGHT_MANAGE_THREAD"}, {1u << 19, "<unknown (1 << 19)>"},
      {1u << 20, "<unknown (1 << 20)>"},    {1u << 21, "<unknown (1 << 21)>"},
      {1u << 22, "<unknown (1 << 22)>"},    {1u << 23, "<unknown (1 << 23)>"},
      {1u << 24, "<unknown (1 << 24)>"},    {1u << 25, "<unknown (1 << 25)>"},
      {1u << 26, "<unknown (1 << 26)>"},    {1u << 27, "<unknown (1 << 27)>"},
      {1u << 28, "<unknown (1 << 28)>"},    {1u << 29, "<unknown (1 << 29)>"},
      {1u << 30, "<unknown (1 << 30)>"},    {1u << 31, "ZX_RIGHT_SAME_RIGHTS"},
  };

  for (const auto& mapping : kRightMapping) {
    if (handle_rights & mapping.bit_value)
      result.emplace_back(mapping.name);
  }

  return result;
}

std::string HandleRightsToString(uint32_t handle_rights) {
  auto rights = HandleRightsToStrings(handle_rights);

  std::string result;
  for (size_t i = 0; i < rights.size(); i++) {
    if (i > 0)
      result += " | ";
    result += rights[i];
  }

  return result;
}

}  // namespace debug_ipc
