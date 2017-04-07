// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Here live the helper functions that define the keyspace for all data in the
// ledger.

#ifndef APPS_MODULAR_LIB_STORAGE_LEDGER_H_
#define APPS_MODULAR_LIB_STORAGE_LEDGER_H_

#include <string>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"

namespace modular {

// message queue => message queue token
std::string MakeMessageQueueTokenKey(const std::string& component_namespace,
                                     const std::string& component_instance_id,
                                     const std::string& queue_name);

// message queue token => message queue
std::string MakeMessageQueueKey(const std::string& queue_token);

// Encodes a list of module names to a ':'-separated module path.
std::string EncodeModulePath(const fidl::Array<fidl::String>& path);
// Encodes the namespace used for modules.
std::string EncodeModuleComponentNamespace(const std::string& story_id);

// link name => link data
std::string MakeLinkKey(const fidl::Array<fidl::String>& path,
                            const fidl::String& link_id);

// agent task id => trigger data
std::string MakeTriggerKey(const std::string& agent_url,
                           const std::string& task_id);

}  // namespace modular

#endif  // APPS_MODULAR_LIB_STORAGE_LEDGER_H_