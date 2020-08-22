// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_EXCEPTION_SETTINGS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_EXCEPTION_SETTINGS_H_

#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/common/err_or.h"

namespace zxdb {

// Two-to-three character shorthands for exception types.
constexpr char kGeneralExcpTypeShorthand[] = "gen";
constexpr char kPageFaultExcpTypeShorthand[] = "pf";
constexpr char kUndefinedInstructionExcpTypeShorthand[] = "ui";
constexpr char kUnalignedAccessExcpTypeShorthand[] = "ua";
constexpr char kPolicyErrorExcpTypeShorthand[] = "pe";

// Returns kNone if the shorthand is not recognized.
debug_ipc::ExceptionType ToExceptionType(const std::string& shorthand);

// Given a list of exception type shorthands, this utility returns the list of
// request objects to update the strategies of those types as second-chance
// and their complement as first-chance.
ErrOr<std::vector<debug_ipc::UpdateGlobalSettingsRequest::UpdateExceptionStrategy>>
ParseExceptionStrategyUpdates(const std::vector<std::string>& second_chance_shorthands);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_EXCEPTION_SETTINGS_H_
