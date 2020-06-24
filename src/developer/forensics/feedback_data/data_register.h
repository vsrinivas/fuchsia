// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_REGISTER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_REGISTER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/datastore.h"

namespace forensics {
namespace feedback_data {

// Registers data useful to attach in feedback reports (crash, user feedback or bug reports).
class DataRegister : public fuchsia::feedback::ComponentDataRegister {
 public:
  explicit DataRegister(Datastore* datastore);

  // |fuchsia.feedback.ComponentDataRegister|
  void Upsert(fuchsia::feedback::ComponentData data, UpsertCallback callback);

  // Exposed for testing purposes.
  const std::map<std::string, Annotations>& GetNamespacedAnnotations() const {
    return namespaced_annotations_;
  };

 private:
  Datastore* datastore_;

  std::map<std::string, Annotations> namespaced_annotations_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_REGISTER_H_
