// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_AGGREGATE_AND_UPLOAD_IMPL_H_
#define SRC_COBALT_BIN_APP_AGGREGATE_AND_UPLOAD_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

class AggregateAndUploadImpl : public fuchsia::cobalt::AggregateAndUpload {
 public:
  AggregateAndUploadImpl();

  void AggregateAndUploadMetricEvents(AggregateAndUploadMetricEventsCallback callback) override;
};
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_AGGREGATE_AND_UPLOAD_IMPL_H_
