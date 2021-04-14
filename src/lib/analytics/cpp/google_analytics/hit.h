// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_HIT_H_
#define SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_HIT_H_

#include <string>
#include <string_view>

#include "src/lib/analytics/cpp/google_analytics/general_parameters.h"

namespace analytics::google_analytics {

class Hit {
 public:
  static constexpr char kHitTypeKey[] = "t";

  // Adds general parameters (not specific to a specific hit type), for example, av (application
  // version)
  void AddGeneralParameters(const GeneralParameters& general_parameters);

  // Represents a hit in parameter form
  // e.g. {"ec": "category", "ea": "action", "el": "label"}
  const std::map<std::string, std::string>& parameters() const { return parameters_; }

  // Hit is an abstract class
  virtual ~Hit() = 0;

 protected:
  void SetParameter(std::string name, std::string_view value);

 private:
  std::map<std::string, std::string> parameters_;
};

}  // namespace analytics::google_analytics

#endif  // SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_HIT_H_
