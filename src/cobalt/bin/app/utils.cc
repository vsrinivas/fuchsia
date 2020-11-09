// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/utils.h"

#include <lib/syslog/cpp/macros.h>

#include "third_party/cobalt/src/lib/util/file_util.h"
#include "third_party/cobalt/src/lib/util/pem_util.h"

namespace cobalt {

using observation_store::ObservationStore;

fuchsia::cobalt::Status ToCobaltStatus(ObservationStore::StoreStatus s) {
  switch (s) {
    case ObservationStore::kOk:
      return fuchsia::cobalt::Status::OK;

    case ObservationStore::kObservationTooBig:
      return fuchsia::cobalt::Status::EVENT_TOO_BIG;

    case ObservationStore::kStoreFull:
      return fuchsia::cobalt::Status::BUFFER_FULL;

    case ObservationStore::kWriteFailed:
      return fuchsia::cobalt::Status::INTERNAL_ERROR;
  }
}

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

fuchsia::cobalt::Status ToCobaltStatus(util::Status s) {
  switch (s.error_code()) {
    case util::StatusCode::OK:
      return fuchsia::cobalt::Status::OK;
    case util::StatusCode::INVALID_ARGUMENT:
      return fuchsia::cobalt::Status::INVALID_ARGUMENTS;
    case util::StatusCode::RESOURCE_EXHAUSTED:
      return fuchsia::cobalt::Status::BUFFER_FULL;
    case util::StatusCode::CANCELLED:
    case util::StatusCode::UNKNOWN:
    case util::StatusCode::DEADLINE_EXCEEDED:
    case util::StatusCode::NOT_FOUND:
    case util::StatusCode::ALREADY_EXISTS:
    case util::StatusCode::PERMISSION_DENIED:
    case util::StatusCode::FAILED_PRECONDITION:
    case util::StatusCode::ABORTED:
    case util::StatusCode::OUT_OF_RANGE:
    case util::StatusCode::UNIMPLEMENTED:
    case util::StatusCode::INTERNAL:
    case util::StatusCode::UNAVAILABLE:
    case util::StatusCode::DATA_LOSS:
    case util::StatusCode::UNAUTHENTICATED:
    default:
      return fuchsia::cobalt::Status::INTERNAL_ERROR;
  }
}

fuchsia::metrics::Status ToMetricsStatus(ObservationStore::StoreStatus s) {
  switch (s) {
    case ObservationStore::kOk:
      return fuchsia::metrics::Status::OK;

    case ObservationStore::kObservationTooBig:
      return fuchsia::metrics::Status::EVENT_TOO_BIG;

    case ObservationStore::kStoreFull:
      return fuchsia::metrics::Status::BUFFER_FULL;

    case ObservationStore::kWriteFailed:
      return fuchsia::metrics::Status::INTERNAL_ERROR;
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

fuchsia::metrics::Status ToMetricsStatus(util::Status s) {
  switch (s.error_code()) {
    case util::StatusCode::OK:
      return fuchsia::metrics::Status::OK;
    case util::StatusCode::INVALID_ARGUMENT:
      return fuchsia::metrics::Status::INVALID_ARGUMENTS;
    case util::StatusCode::RESOURCE_EXHAUSTED:
      return fuchsia::metrics::Status::BUFFER_FULL;
    case util::StatusCode::CANCELLED:
    case util::StatusCode::UNKNOWN:
    case util::StatusCode::DEADLINE_EXCEEDED:
    case util::StatusCode::NOT_FOUND:
    case util::StatusCode::ALREADY_EXISTS:
    case util::StatusCode::PERMISSION_DENIED:
    case util::StatusCode::FAILED_PRECONDITION:
    case util::StatusCode::ABORTED:
    case util::StatusCode::OUT_OF_RANGE:
    case util::StatusCode::UNIMPLEMENTED:
    case util::StatusCode::INTERNAL:
    case util::StatusCode::UNAVAILABLE:
    case util::StatusCode::DATA_LOSS:
    case util::StatusCode::UNAUTHENTICATED:
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
