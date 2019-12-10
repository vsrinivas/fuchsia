// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_SCOPED_TMP_LOCATION_H_
#define SRC_LEDGER_BIN_PLATFORM_SCOPED_TMP_LOCATION_H_

namespace ledger {

// A temporary storage location that will be destroyed when this class is deleted.
class ScopedTmpLocation {
 public:
  ScopedTmpLocation() = default;
  virtual ~ScopedTmpLocation() = default;

  ScopedTmpLocation(const ScopedTmpLocation&) = delete;
  ScopedTmpLocation& operator=(const ScopedTmpLocation&) = delete;

  virtual DetachedPath path() = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_SCOPED_TMP_LOCATION_H_
