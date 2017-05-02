// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "apps/maxwell/services/user/scope.fidl.h"

namespace maxwell {

std::string ScopeAndTopicToString(const ComponentScopePtr& scope,
                                  const std::string& topic);

std::string MakeStoryScopeTopic(const std::string& story_id,
                                const std::string& topic);

std::string MakeModuleScopeTopic(const std::string& story_id,
                                 const std::string& module_id,
                                 const std::string& topic);

bool ParseModuleScopeTopic(const std::string& full_topic,
                           std::string* story_id,
                           std::string* module_id,
                           std::string* relative_topic);

}  // namespace maxwell
