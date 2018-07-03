// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_PUBLIC_TYPES_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_PUBLIC_TYPES_H_

#include <string>

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/cloud_provider_firebase/gcs/status.h"
#include "peridot/lib/firebase/status.h"

namespace cloud_provider_firebase {

using AppId = std::string;
using PageId = std::string;
using CommitId = std::string;
using ObjectDigest = std::string;
using ObjectDigestView = fxl::StringView;
using Data = std::string;

enum class Status {
  OK,
  ARGUMENT_ERROR,
  INTERNAL_ERROR,
  NETWORK_ERROR,
  NOT_FOUND,
  PARSE_ERROR,
  SERVER_ERROR,
};

fxl::StringView StatusToString(Status status);
std::ostream& operator<<(std::ostream& os, Status status);

Status ConvertGcsStatus(gcs::Status gcs_status);

Status ConvertFirebaseStatus(firebase::Status firebase_status);

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_PUBLIC_TYPES_H_
