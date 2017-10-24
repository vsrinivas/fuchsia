// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_CONVERT_STATUS_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_CONVERT_STATUS_H_

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/types.h"

namespace cloud_provider_firebase {

// TODO(ppi): drop internal status and use cloud_provider::Status everywhere
// inside cloud_provider_firebase.
cloud_provider::Status ConvertInternalStatus(Status status);

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_CONVERT_STATUS_H_
