// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_GENERAL_PARAMETERS_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_GENERAL_PARAMETERS_H_

#include "src/lib/analytics/cpp/google_analytics/general_parameters.h"

namespace analytics::core_dev_tools {

class GeneralParameters : public google_analytics::GeneralParameters {
 public:
  // Explicitly pick which parameters to expose
  using google_analytics::GeneralParameters::SetApplicationName;
  using google_analytics::GeneralParameters::SetApplicationVersion;

  void SetOsVersion(std::string_view os);
};

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_GENERAL_PARAMETERS_H_
