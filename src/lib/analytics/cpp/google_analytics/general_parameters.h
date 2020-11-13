// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_GENERAL_PARAMETERS_H_
#define SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_GENERAL_PARAMETERS_H_

#include <map>
#include <string>
#include <string_view>

namespace analytics::google_analytics {

// Parameters that are not specific to one hit type.
// This is an abstract class. To use this class, one must inherit this class and expose
// parameters that will be actually used. For example,
//
//     class Derived : public GeneralParameters {
//      public:
//       using GeneralParameters::SetApplicationName;
//       using GeneralParameters::SetApplicationVersion;
//
//       void SetOsVersion() { SetCustomDimension(1, ...) }
//     }
//
class GeneralParameters {
 public:
  virtual ~GeneralParameters() = 0;

  const std::map<std::string, std::string>& parameters() const { return parameters_; }

 protected:
  void SetCustomDimension(int index, std::string_view value);
  void SetApplicationName(std::string_view application_name);
  void SetApplicationVersion(std::string_view application_version);

 private:
  std::map<std::string, std::string> parameters_;
};

}  // namespace analytics::google_analytics

#endif  // SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_GENERAL_PARAMETERS_H_
