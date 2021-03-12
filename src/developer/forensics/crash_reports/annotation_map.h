// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ANNOTATION_MAP_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ANNOTATION_MAP_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <initializer_list>
#include <map>
#include <string>
#include <type_traits>

#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics::crash_reports {

class AnnotationMap {
 public:
  AnnotationMap() = default;

  template <typename T>
  explicit AnnotationMap(const std::map<std::string, T>& init) {
    for (const auto& [k, v] : init) {
      Set(k, v);
    }
  }

  // Construct from an initializer list.
  AnnotationMap(std::initializer_list<std::pair<const std::string, std::string>> init) {
    for (const auto& [k, v] : init) {
      Set(k, v);
    }
  }

  // A type that can be converted to std::string.
  template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string>, bool> = true>
  AnnotationMap& Set(const std::string& key, const T& val) {
    data_[key] = val;
    return *this;
  }

  // A type that cannot be converted to std::string.
  template <typename T, std::enable_if_t<!std::is_convertible_v<T, std::string>, bool> = true>
  AnnotationMap& Set(const std::string& key, const T& val) {
    data_[key] = std::to_string(val);
    return *this;
  }

  // Bools.
  AnnotationMap& Set(const std::string& key, const bool val) {
    data_[key] = (val) ? "true" : "false";
    return *this;
  }

  // Generic ErrorOrs.
  template <typename T>
  AnnotationMap& Set(const std::string& key, const ErrorOr<T>& val) {
    // Set the annotation as with the value or "unknown" and add a value under
    // "debug.$key.error" explaining why.
    return (val.HasValue())
               ? Set(key, val.Value())
               : Set(key, "unknown")
                     .Set(fxl::StringPrintf("debug.%s.error", key.c_str()), val.Error());
  }

  // Errors.
  AnnotationMap& Set(const std::string& key, const Error& error) {
    return Set(key, ToReason(error));
  }

  // FIDL annotations.
  AnnotationMap& Set(const ::fuchsia::feedback::Annotation& annotation) {
    data_[annotation.key] = annotation.value;
    return *this;
  }

  // Another AnnotationMap.
  AnnotationMap& Set(const AnnotationMap& annotations) {
    for (const auto& [k, v] : annotations.Raw()) {
      Set(k, v);
    }
    return *this;
  }

  // Vectors of FIDL annotations.
  AnnotationMap& Set(const std::vector<::fuchsia::feedback::Annotation>& annotations) {
    for (const auto& annotation : annotations) {
      Set(annotation);
    }
    return *this;
  }

  // Generic maps.
  template <typename T>
  AnnotationMap& Set(const std::map<std::string, T>& annotations) {
    for (const auto& [key, val] : annotations) {
      Set(key, val);
    }
    return *this;
  }

  bool Contains(const std::string& key) const { return data_.find(key) != data_.end(); }

  const std::string& Get(const std::string& key) const {
    FX_CHECK(Contains(key));
    return data_.at(key);
  }

  const std::map<std::string, std::string>& Raw() const { return data_; }

 private:
  std::map<std::string, std::string> data_;
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_ANNOTATION_MAP_H_
