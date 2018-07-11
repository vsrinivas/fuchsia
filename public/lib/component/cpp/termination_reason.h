// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_TERMINATION_REASON_H_
#define LIB_COMPONENT_CPP_TERMINATION_REASON_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <string>

namespace component {

std::string TerminationReasonToString(
    fuchsia::sys::TerminationReason termination_reason);

std::string HumanReadableTerminationReason(
    fuchsia::sys::TerminationReason termination_reason);

}  // namespace component

#endif  // LIB_COMPONENT_CPP_TERMINATION_REASON_H_
