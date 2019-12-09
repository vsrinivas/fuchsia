// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_TYPES_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_TYPES_H_

namespace ledger {

enum class CloudEraseOnCheck { YES, NO };
enum class CloudEraseFromWatcher { YES, NO };
enum class InjectNetworkError { YES, NO };

// This is used to test Ledger's compatibility with non diff-enabled peers and cloud providers.
enum class InjectMissingDiff {
  // The cloud always accepts diffs sent by clients.
  //
  // This is used to test Ledger's non-compatible mode of operation.
  YES,
  // The cloud discards diffs sent by clients with 50% probability.
  //
  // This is used to test the compatibility strategy: missing a diff is the observable consequence
  // of either having a cloud provider that did not support diffs when the commit was uploaded, or
  // that the commit was uploaded by a non diff-supporting Ledger.
  NO
};

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_TYPES_H_
