// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "lib/module_resolver/fidl/module_resolver.fidl.h"

namespace maxwell {

class NounTypeInferenceHelper {
 public:
  NounTypeInferenceHelper();

  // Returns a list of Entity types represented in |noun|. Chooses the correct
  // process for type extraction based on the type of Noun.
  std::vector<std::string> GetEntityTypes(const modular::NounPtr& noun);

  FXL_DISALLOW_COPY_AND_ASSIGN(NounTypeInferenceHelper);
};

}  // namespace maxwell
