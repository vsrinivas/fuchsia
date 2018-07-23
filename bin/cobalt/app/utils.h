// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_UTILS_H_
#define GARNET_BIN_COBALT_APP_UTILS_H_

#include <stdlib.h>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "third_party/cobalt/encoder/shipping_manager.h"

namespace cobalt {

// Maps an ObservationStore::StoreStatus to a fuchsia::cobalt::Status.
fuchsia::cobalt::Status ToCobaltStatus(
    encoder::ObservationStore::StoreStatus s);

// Reads the PEM file at the specified path and returns the contents as
// a string. CHECK fails if the file cannot be read.
std::string ReadPublicKeyPem(const std::string& pem_file_path);

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_UTILS_H_
