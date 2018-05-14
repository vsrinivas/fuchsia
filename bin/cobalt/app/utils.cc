// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/utils.h"

#include "third_party/cobalt/util/pem_util.h"

namespace cobalt {

using encoder::ShippingManager;

cobalt::Status ToCobaltStatus(ShippingManager::Status s) {
  switch (s) {
    case ShippingManager::kOk:
      return Status::OK;

    case ShippingManager::kObservationTooBig:
      return Status::OBSERVATION_TOO_BIG;

    case ShippingManager::kFull:
      return Status::TEMPORARILY_FULL;

    case ShippingManager::kShutDown:
    case ShippingManager::kEncryptionFailed:
      return Status::INTERNAL_ERROR;
  }
}

std::string ReadPublicKeyPem(const std::string& pem_file_path) {
  VLOG(2) << "Reading PEM file at " << pem_file_path;
  std::string pem_out;
  FXL_CHECK(util::PemUtil::ReadTextFile(pem_file_path, &pem_out))
      << "Unable to read file public key PEM file from path " << pem_file_path
      << ".";
  return pem_out;
}

}  // namespace cobalt
