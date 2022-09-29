// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/data_provider.h"

#include <lib/fpromise/result.h>
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

std::vector<Annotation> BuildFidlAnnotations(
    const std::map<std::string, std::string>& annotations) {
  std::vector<Annotation> ret_annotations;
  for (const auto& [key, value] : annotations) {
    ret_annotations.push_back({key, value});
  }
  return ret_annotations;
}

feedback::Annotations BuildFeedbackAnnotations(
    const std::map<std::string, std::string>& annotations) {
  feedback::Annotations ret_annotations;
  for (const auto& [key, value] : annotations) {
    ret_annotations.insert({key, value});
  }
  return ret_annotations;
}

Attachment BuildAttachment(const std::string& key) {
  Attachment attachment;
  attachment.key = key;
  FX_CHECK(fsl::VmoFromString("", &attachment.value));
  return attachment;
}

}  // namespace

void DataProvider::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                               GetSnapshotCallback callback) {
  Snapshot snapshot;
  snapshot.set_annotations(BuildFidlAnnotations(annotations_));
  snapshot.set_archive(BuildAttachment(snapshot_key_));
  callback(std::move(snapshot));
}

void DataProvider::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  callback(BuildFeedbackAnnotations(annotations_), BuildAttachment(snapshot_key_));
}

void DataProviderReturnsNoAnnotation::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                  GetSnapshotCallback callback) {
  callback(std::move(Snapshot().set_archive(BuildAttachment(snapshot_key_))));
}

void DataProviderReturnsNoAnnotation::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  callback({}, BuildAttachment(snapshot_key_));
}

void DataProviderReturnsNoAttachment::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                  GetSnapshotCallback callback) {
  callback(std::move(Snapshot().set_annotations(BuildFidlAnnotations(annotations_))));
}

void DataProviderReturnsNoAttachment::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  callback(BuildFeedbackAnnotations(annotations_), {});
}

void DataProviderReturnsEmptySnapshot::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                                   GetSnapshotCallback callback) {
  callback(Snapshot());
}

void DataProviderReturnsEmptySnapshot::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  callback({}, {});
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

void DataProviderTracksNumConnections::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  FX_LOGS(FATAL) << "Unexpected call to GetSnapshotInternal";
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

void DataProviderTracksNumCalls::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  ++num_calls_;
  callback({}, {});
}

void DataProviderReturnsOnDemand::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                              GetSnapshotCallback callback) {
  snapshot_callbacks_.push(std::move(callback));
}

void DataProviderReturnsOnDemand::GetSnapshotInternal(
    const zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  snapshot_internal_callbacks_.push(std::move(callback));
}

void DataProviderReturnsOnDemand::PopSnapshotCallback() {
  FX_CHECK(!snapshot_callbacks_.empty());

  Snapshot snapshot;
  snapshot.set_annotations(BuildFidlAnnotations(annotations_));
  snapshot.set_archive(BuildAttachment(snapshot_key_));

  snapshot_callbacks_.front()(std::move(snapshot));
  snapshot_callbacks_.pop();
}

void DataProviderReturnsOnDemand::PopSnapshotInternalCallback() {
  FX_CHECK(!snapshot_internal_callbacks_.empty());

  snapshot_internal_callbacks_.front()(BuildFeedbackAnnotations(annotations_),
                                       BuildAttachment(snapshot_key_));
  snapshot_internal_callbacks_.pop();
}

void DataProviderSnapshotOnly::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                                           GetSnapshotCallback callback) {
  callback(std::move(Snapshot().set_archive(std::move(snapshot_))));
}

void DataProviderSnapshotOnly::GetSnapshotInternal(
    zx::duration timeout,
    fit::callback<void(feedback::Annotations, fuchsia::feedback::Attachment)> callback) {
  callback({}, std::move(snapshot_));
}

}  // namespace stubs
}  // namespace forensics
