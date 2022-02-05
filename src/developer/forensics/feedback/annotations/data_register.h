// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DATA_REGISTER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DATA_REGISTER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace forensics::feedback {

// Registers data useful to attach in feedback reports (crash, user feedback or bug reports).
class DataRegister : public fuchsia::feedback::ComponentDataRegister,
                     public NonPlatformAnnotationProvider {
 public:
  DataRegister(size_t max_num_annotations, std::set<std::string> disallowed_annotation_namespaces,
               std::string register_filepath);

  // |fuchsia.feedback.ComponentDataRegister|
  void Upsert(fuchsia::feedback::ComponentData data, UpsertCallback callback) override;

  // |forensics.feedback.DynamicAnnotationProvider|
  //
  // Returns the non-platform annotations.
  Annotations Get() override;

  // |forensics.feedback.NonPlatformAnnotationProvider|
  //
  // Returns true if non-platform annotations are missing, e.g. a call to Upsert would have put us
  // over |max_num_annotations_|.
  bool IsMissingAnnotations() const override { return is_missing_annotations_; }

 private:
  void RestoreFromJson();
  void UpdateJson(const std::string& _namespace, const Annotations& annotations);

  size_t max_num_annotations_;
  std::set<std::string> disallowed_annotation_namespaces_;
  std::string register_filepath_;

  std::map<std::string, Annotations> namespaced_annotations_;
  bool is_missing_annotations_{false};

  rapidjson::Document register_json_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DATA_REGISTER_H_
