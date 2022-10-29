// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ITEM_LOCATION_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ITEM_LOCATION_H_

namespace forensics::crash_reports {

// Indicates where a piece of infomormation is located.
enum class ItemLocation {
  kMemory,
  kCache,
  kTmp,
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ITEM_LOCATION_H_
