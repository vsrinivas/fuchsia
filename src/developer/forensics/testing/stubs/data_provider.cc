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
using fuchsia::feedback::Snapshot;

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

void DataProvider::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                               GetSnapshotCallback callback) {
  Snapshot snapshot;
  snapshot.set_annotations(BuildAnnotations(annotations_));
  snapshot.set_archive(BuildAttachment(snapshot_key_));
  callback(std::move(snapshot));
}

void DataProviderReturnsNoAnnotation::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                  GetSnapshotCallback callback) {
  callback(std::move(Snapshot().set_archive(BuildAttachment(snapshot_key_))));
}

void DataProviderReturnsNoAttachment::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                  GetSnapshotCallback callback) {
  callback(std::move(Snapshot().set_annotations(BuildAnnotations(annotations_))));
}

void DataProviderReturnsEmptySnapshot::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                   GetSnapshotCallback callback) {
  callback(Snapshot());
}

DataProviderTracksNumConnections::~DataProviderTracksNumConnections() {
  FX_CHECK(expected_num_connections_ == num_connections_)
      << "Expected " << expected_num_connections_ << " connections\n"
      << "Made " << num_connections_ << " connections";
}

void DataProviderTracksNumConnections::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                   GetSnapshotCallback callback) {
  callback(Snapshot());
}

DataProviderTracksNumCalls::~DataProviderTracksNumCalls() {
  FX_CHECK(expected_num_calls_ == num_calls_) << "Expected " << expected_num_calls_ << " calls\n"
                                              << "Made " << num_calls_ << " calls";
}

void DataProviderTracksNumCalls::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                             GetSnapshotCallback callback) {
  ++num_calls_;
  callback(Snapshot());
}

void DataProviderSnapshotOnly::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                           GetSnapshotCallback callback) {
  callback(std::move(Snapshot().set_archive(std::move(snapshot_))));
}

}  // namespace stubs
}  // namespace forensics
