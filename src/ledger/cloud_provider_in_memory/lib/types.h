// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_TYPES_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_TYPES_H_

namespace ledger {

enum class CloudEraseOnCheck { YES, NO };
enum class CloudEraseFromWatcher { YES, NO };
enum class InjectNetworkError { YES, NO };

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_TYPES_H_
