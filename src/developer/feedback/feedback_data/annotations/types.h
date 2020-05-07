// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_TYPES_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_TYPES_H_

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <set>
#include <string>
#include <variant>

#include "src/developer/feedback/utils/errors.h"

namespace feedback {

using AnnotationKey = std::string;
using AnnotationKeys = std::set<AnnotationKey>;

class AnnotationOr {
 public:
  AnnotationOr(const std::string& value) : data_(value) {}
  AnnotationOr(enum Error error) : data_(error) {}

  bool HasValue() const { return data_.index() == 0; }

  const std::string& Value() const {
    FX_CHECK(HasValue());
    return std::get<std::string>(data_);
  }

  enum Error Error() const {
    FX_CHECK(!HasValue());
    return std::get<enum Error>(data_);
  }

  bool operator==(const AnnotationOr& other) const { return data_ == other.data_; }

 private:
  std::variant<std::string, enum Error> data_;
};

using Annotations = std::map<AnnotationKey, AnnotationOr>;

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_TYPES_H_
