// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_RECORD_H_
#define APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_RECORD_H_

#include "apps/ledger/cloud_provider/public/notification.h"

namespace cloud_provider {

// Represents a notification along with its timestamp.
// TODO(ppi): this currently makes a copy of the notification. We should
//            probably make Notification movable and take an rvalue reference.
struct Record {
  Record(const Notification& n, std::string t);

  Notification notification;
  std::string timestamp;
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_CLOUD_PROVIDER_PUBLIC_RECORD_H_
