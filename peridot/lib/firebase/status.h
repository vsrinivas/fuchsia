// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_STATUS_H_
#define PERIDOT_LIB_FIREBASE_STATUS_H_

#include <iostream>

#include <lib/fxl/strings/string_view.h>

namespace firebase {

enum class Status { OK, NETWORK_ERROR, PARSE_ERROR, SERVER_ERROR };

fxl::StringView StatusToString(Status status);

std::ostream& operator<<(std::ostream& os, Status status);

}  // namespace firebase

#endif  // PERIDOT_LIB_FIREBASE_STATUS_H_
