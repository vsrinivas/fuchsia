// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/function.h>

#include <set>
#include <string>

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"
#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

struct DeviceIdToAnnotations {
  Annotations operator()(const ErrorOr<std::string>& device_id);
};

// Fetches the device id from the file at |path|.
class LocalDeviceIdProvider : public CachedAsyncAnnotationProvider {
 public:
  explicit LocalDeviceIdProvider(const std::string& path);

  void GetOnUpdate(::fit::function<void(Annotations)> callback) override;

  std::set<std::string> GetKeys() const override;

 private:
  std::string device_id_;
};

// Fetches the device id from a FIDL server.
class RemoteDeviceIdProvider
    : public HangingGetSingleFidlMethodAnnotationProvider<
          fuchsia::feedback::DeviceIdProvider, &fuchsia::feedback::DeviceIdProvider::GetId,
          DeviceIdToAnnotations> {
 public:
  using HangingGetSingleFidlMethodAnnotationProvider::HangingGetSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
