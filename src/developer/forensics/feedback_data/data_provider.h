// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/vmo_file.h>

#include <functional>
#include <map>
#include <memory>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback_data/datastore.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/developer/forensics/feedback_data/metadata.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {
namespace feedback_data {

// Serves snapshot archive through a channel using |fuchsia.io.File|
class ServedArchive {
 public:
  explicit ServedArchive(fsl::SizedVmo data);
  bool Serve(zx::channel server_end, async_dispatcher_t* dispatcher,
             std::function<void()> completed);

 private:
  vfs::VmoFile file_;
  std::unique_ptr<async::WaitOnce> channel_closed_observer_ = nullptr;
};

// Provides data useful to attach in feedback reports (crash, user feedback or bug reports).
class DataProvider : public fuchsia::feedback::DataProvider {
 public:
  DataProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               timekeeper::Clock* clock, bool is_first_instance,
               const AnnotationKeys& annotation_allowlist,
               const AttachmentKeys& attachment_allowlist, cobalt::Logger* cobalt,
               feedback::AnnotationManager* annotation_manager, Datastore* datastore,
               InspectDataBudget* inspect_data_budget);

  // |fuchsia::feedback::DataProvider|
  void GetAnnotations(fuchsia::feedback::GetAnnotationsParameters params,
                      GetAnnotationsCallback callback) override;
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override;

  size_t NumCurrentServedArchives() { return served_archives_.size(); }

 private:
  bool ServeArchive(fsl::SizedVmo archive, zx::channel server_end);

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  Metadata metadata_;
  cobalt::Logger* cobalt_;
  feedback::AnnotationManager* annotation_manager_;
  Datastore* datastore_;
  async::Executor executor_;
  InspectDataBudget* inspect_data_budget_;

  std::map<size_t, std::unique_ptr<ServedArchive>> served_archives_;
  size_t next_served_archive_index_ = 0;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_H_
