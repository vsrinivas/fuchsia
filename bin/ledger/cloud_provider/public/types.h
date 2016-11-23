// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_TYPES_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_TYPES_H_

#include <string>

#include "lib/ftl/strings/string_view.h"

namespace cloud_provider {

using AppId = std::string;
using PageId = std::string;
using CommitId = std::string;
using ObjectId = std::string;
using ObjectIdView = ftl::StringView;
using Data = std::string;

enum class Status {
  OK,
  ARGUMENT_ERROR,
  NETWORK_ERROR,
  INTERNAL_ERROR,
  UNKNOWN_ERROR
};

ftl::StringView StatusToString(Status status);
std::ostream& operator<<(std::ostream& os, Status status);

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_TYPES_H_
