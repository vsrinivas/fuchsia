// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_TIMESTAMP_CONVERSIONS_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_TIMESTAMP_CONVERSIONS_H_

#include <string>

// Firebase Realtime Database uses time-since-epoch as timestamps, which we
// represent as int64_t, until we grow a wall time type in FTL. However,
// CloudProvider API is more general and operates on opaque bytes - these
// functions convert back and forth.

namespace cloud_provider_firebase {

std::string ServerTimestampToBytes(int64_t timestamp);

int64_t BytesToServerTimestamp(const std::string& bytes);

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_TIMESTAMP_CONVERSIONS_H_
