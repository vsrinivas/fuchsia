// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/feedback_data/datastore.h"
#include "src/developer/forensics/feedback_data/integrity_reporter.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics {
namespace feedback_data {

// Provides data useful to attach in feedback reports (crash, user feedback or bug reports).
class DataProvider : public fuchsia::feedback::DataProvider {
 public:
  DataProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               IntegrityReporter integrity_reporter, cobalt::Logger* cobalt, Datastore* datastore);

  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const IntegrityReporter integrity_reporter_;
  cobalt::Logger* cobalt_;
  Datastore* datastore_;
  async::Executor executor_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_H_
