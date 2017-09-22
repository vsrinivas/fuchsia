// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_TYPES_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_TYPES_H_

#include <string>

#include "peridot/bin/ledger/firebase/status.h"
#include "peridot/bin/ledger/gcs/status.h"
#include "lib/fxl/strings/string_view.h"

namespace cloud_provider_firebase {

using AppId = std::string;
using PageId = std::string;
using CommitId = std::string;
using ObjectId = std::string;
using ObjectIdView = fxl::StringView;
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

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_TYPES_H_
