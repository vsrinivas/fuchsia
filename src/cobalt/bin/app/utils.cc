// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/utils.h"

#include <lib/syslog/cpp/macros.h>

#include "third_party/cobalt/src/lib/util/file_util.h"
#include "third_party/cobalt/src/lib/util/pem_util.h"
#include "third_party/cobalt/src/public/lib/status.h"

namespace cobalt {

fuchsia::cobalt::Status ToCobaltStatus(logger::Status s) {
  switch (s) {
    case logger::Status::kOK:
      return fuchsia::cobalt::Status::OK;

    case logger::Status::kInvalidArguments:
    case logger::Status::kInvalidConfig:
      return fuchsia::cobalt::Status::INVALID_ARGUMENTS;

    case logger::Status::kTooBig:
      return fuchsia::cobalt::Status::EVENT_TOO_BIG;

    case logger::Status::kFull:
      return fuchsia::cobalt::Status::BUFFER_FULL;

    case logger::Status::kOther:
      return fuchsia::cobalt::Status::INTERNAL_ERROR;

    default:  // Needed so that the Cobalt core enum can be added to.
      FX_LOGS(ERROR) << "Unimplemented translation for enum value: " << s;
      return fuchsia::cobalt::Status::INTERNAL_ERROR;
  }
}

fuchsia::cobalt::Status ToCobaltStatus(Status s) {
  switch (s.error_code()) {
    case StatusCode::OK:
      return fuchsia::cobalt::Status::OK;
    case StatusCode::INVALID_ARGUMENT:
      return fuchsia::cobalt::Status::INVALID_ARGUMENTS;
    case StatusCode::RESOURCE_EXHAUSTED:
      return fuchsia::cobalt::Status::BUFFER_FULL;
    case StatusCode::CANCELLED:
    case StatusCode::UNKNOWN:
    case StatusCode::DEADLINE_EXCEEDED:
    case StatusCode::NOT_FOUND:
    case StatusCode::ALREADY_EXISTS:
    case StatusCode::PERMISSION_DENIED:
    case StatusCode::FAILED_PRECONDITION:
    case StatusCode::ABORTED:
    case StatusCode::OUT_OF_RANGE:
    case StatusCode::UNIMPLEMENTED:
    case StatusCode::INTERNAL:
    case StatusCode::UNAVAILABLE:
    case StatusCode::DATA_LOSS:
    case StatusCode::UNAUTHENTICATED:
    default:
      return fuchsia::cobalt::Status::INTERNAL_ERROR;
  }
}

fuchsia::metrics::Status ToMetricsStatus(logger::Status s) {
  switch (s) {
    case logger::Status::kOK:
      return fuchsia::metrics::Status::OK;

    case logger::Status::kInvalidArguments:
    case logger::Status::kInvalidConfig:
      return fuchsia::metrics::Status::INVALID_ARGUMENTS;

    case logger::Status::kTooBig:
      return fuchsia::metrics::Status::EVENT_TOO_BIG;

    case logger::Status::kFull:
      return fuchsia::metrics::Status::BUFFER_FULL;

    case logger::Status::kOther:
      return fuchsia::metrics::Status::INTERNAL_ERROR;

    default:  // Needed so that the Cobalt core enum can be added to.
      FX_LOGS(ERROR) << "Unimplemented translation for enum value: " << s;
      return fuchsia::metrics::Status::INTERNAL_ERROR;
  }
}

fuchsia::metrics::Status ToMetricsStatus(Status s) {
  switch (s.error_code()) {
    case StatusCode::OK:
      return fuchsia::metrics::Status::OK;
    case StatusCode::INVALID_ARGUMENT:
      return fuchsia::metrics::Status::INVALID_ARGUMENTS;
    case StatusCode::RESOURCE_EXHAUSTED:
      return fuchsia::metrics::Status::BUFFER_FULL;
    case StatusCode::CANCELLED:
    case StatusCode::UNKNOWN:
    case StatusCode::DEADLINE_EXCEEDED:
    case StatusCode::NOT_FOUND:
    case StatusCode::ALREADY_EXISTS:
    case StatusCode::PERMISSION_DENIED:
    case StatusCode::FAILED_PRECONDITION:
    case StatusCode::ABORTED:
    case StatusCode::OUT_OF_RANGE:
    case StatusCode::UNIMPLEMENTED:
    case StatusCode::INTERNAL:
    case StatusCode::UNAVAILABLE:
    case StatusCode::DATA_LOSS:
    case StatusCode::UNAUTHENTICATED:
    default:
      return fuchsia::metrics::Status::INTERNAL_ERROR;
  }
}

std::string ReadPublicKeyPem(const std::string& pem_file_path) {
  FX_VLOGS(2) << "Reading PEM file at " << pem_file_path;
  std::string pem_out;
  FX_CHECK(util::PemUtil::ReadTextFile(pem_file_path, &pem_out))
      << "Unable to read file public key PEM file from path " << pem_file_path << ".";
  return pem_out;
}

}  // namespace cobalt
