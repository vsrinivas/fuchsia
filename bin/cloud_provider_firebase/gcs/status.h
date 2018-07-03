// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_STATUS_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_STATUS_H_

#include <iostream>

#include <lib/fxl/strings/string_view.h>

namespace gcs {

enum class Status {
  OK,
  INTERNAL_ERROR,
  NETWORK_ERROR,
  NOT_FOUND,
  OBJECT_ALREADY_EXISTS,
  PARSE_ERROR,
  SERVER_ERROR
};

fxl::StringView StatusToString(Status status);

inline std::ostream& operator<<(std::ostream& os, Status status) {
  return os << StatusToString(status);
}

}  // namespace gcs

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_STATUS_H_
