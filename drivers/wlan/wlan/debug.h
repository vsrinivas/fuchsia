// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "mac_frame.h"

namespace wlan {
namespace debug {

std::string Describe(const SequenceControl& sc);
std::string Describe(const FrameHeader& hdr);
std::string Describe(const DataFrameHeader& hdr);

}  // namespace debug
}  // namespace wlan
