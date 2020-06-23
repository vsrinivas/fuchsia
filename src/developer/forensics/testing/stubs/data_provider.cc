// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/data_provider.h"

#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <map>
#include <string>

#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace stubs {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::Bugreport;

std::vector<Annotation> BuildAnnotations(const std::map<std::string, std::string>& annotations) {
  std::vector<Annotation> ret_annotations;
  for (const auto& [key, value] : annotations) {
    ret_annotations.push_back({key, value});
  }
  return ret_annotations;
}

Attachment BuildAttachment(const std::string& key) {
  Attachment attachment;
  attachment.key = key;
  FX_CHECK(fsl::VmoFromString("unused", &attachment.value));
  return attachment;
}

}  // namespace

void DataProvider::GetBugreport(fuchsia::feedback::GetBugreportParameters params,
                                GetBugreportCallback callback) {
  Bugreport bugreport;
  bugreport.set_annotations(BuildAnnotations(annotations_));
  bugreport.set_bugreport(BuildAttachment(bugreport_key_));
  callback(std::move(bugreport));
}

void DataProviderReturnsNoAnnotation::GetBugreport(fuchsia::feedback::GetBugreportParameters params,
                                                   GetBugreportCallback callback) {
  callback(std::move(Bugreport().set_bugreport(BuildAttachment(bugreport_key_))));
}

void DataProviderReturnsNoAttachment::GetBugreport(fuchsia::feedback::GetBugreportParameters params,
                                                   GetBugreportCallback callback) {
  callback(std::move(Bugreport().set_annotations(BuildAnnotations(annotations_))));
}

void DataProviderReturnsEmptyBugreport::GetBugreport(
    fuchsia::feedback::GetBugreportParameters params, GetBugreportCallback callback) {
  callback(Bugreport());
}

DataProviderTracksNumConnections::~DataProviderTracksNumConnections() {
  FX_CHECK(expected_num_connections_ == num_connections_)
      << "Expected " << expected_num_connections_ << " connections\n"
      << "Made " << num_connections_ << " connections";
}

void DataProviderTracksNumConnections::GetBugreport(
    fuchsia::feedback::GetBugreportParameters params, GetBugreportCallback callback) {
  callback(Bugreport());
}

void DataProviderBugreportOnly::GetBugreport(fuchsia::feedback::GetBugreportParameters params,
                                             GetBugreportCallback callback) {
  callback(std::move(Bugreport().set_bugreport(std::move(bugreport_))));
}

}  // namespace stubs
}  // namespace forensics
