// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/exception_settings.h"

#include <set>

namespace zxdb {

debug_ipc::ExceptionType ToExceptionType(const std::string& shorthand) {
  if (shorthand == kGeneralExcpTypeShorthand) {
    return debug_ipc::ExceptionType::kGeneral;
  } else if (shorthand == kPageFaultExcpTypeShorthand) {
    return debug_ipc::ExceptionType::kPageFault;
  } else if (shorthand == kUndefinedInstructionExcpTypeShorthand) {
    return debug_ipc::ExceptionType::kUndefinedInstruction;
  } else if (shorthand == kUnalignedAccessExcpTypeShorthand) {
    return debug_ipc::ExceptionType::kUnalignedAccess;
  } else if (shorthand == kPolicyErrorExcpTypeShorthand) {
    return debug_ipc::ExceptionType::kPolicyError;
  }
  return debug_ipc::ExceptionType::kNone;
}

ErrOr<std::vector<debug_ipc::UpdateGlobalSettingsRequest::UpdateExceptionStrategy>>
ParseExceptionStrategyUpdates(const std::vector<std::string>& second_chance_shorthands) {
  std::set<debug_ipc::ExceptionType> second_chance_excps;
  for (const auto& shorthand : second_chance_shorthands) {
    if (auto excp = ToExceptionType(shorthand); excp == debug_ipc::ExceptionType::kNone) {
      return Err("Unrecognized exception type shorthand: %s", shorthand.c_str());
    } else {
      second_chance_excps.insert(excp);
    }
  }
  std::vector<debug_ipc::UpdateGlobalSettingsRequest::UpdateExceptionStrategy> updates;
  for (uint32_t excp = static_cast<uint32_t>(debug_ipc::ExceptionType::kNone);
       excp != static_cast<uint32_t>(debug_ipc::ExceptionType::kLast); ++excp) {
    bool second_chance =
        second_chance_excps.find(debug_ipc::ExceptionType{excp}) != second_chance_excps.end();
    updates.emplace_back(debug_ipc::UpdateGlobalSettingsRequest::UpdateExceptionStrategy{
        .type = debug_ipc::ExceptionType{excp},
        .value = second_chance ? debug_ipc::ExceptionStrategy::kSecondChance
                               : debug_ipc::ExceptionStrategy::kFirstChance,
    });
  }
  return updates;
}

}  // namespace zxdb
