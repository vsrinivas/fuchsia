// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_METRIC_PROPERTIES_METRIC_PROPERTIES_H_
#define SRC_LIB_ANALYTICS_CPP_METRIC_PROPERTIES_METRIC_PROPERTIES_H_

#include <optional>
#include <string>

namespace analytics::metric_properties {

// Functions to read/write metric properties (e.g. UUID, opt-in/out status), which are stored in
// ~/.fuchsia/metrics/<property-name>

// Get the property with the given name. The returned string will contain no leading or trailing
// newlines. Returns an empty std::optional if the property does not exist.
std::optional<std::string> Get(std::string_view name);

// Similar to Get(), but returns a boolean. Returns true if and only if Get() would return "1".
std::optional<bool> GetBool(std::string_view name);

// Set the property with the given name to the given value.
void Set(std::string_view name, std::string_view value);
inline void SetBool(std::string_view name, bool value) { Set(name, value ? "1" : "0"); }

// Delete the property with the given name.
void Delete(std::string_view name);

// Check the existence of the property with the given name.
bool Exists(std::string_view name);

}  // namespace analytics::metric_properties

#endif  // SRC_LIB_ANALYTICS_CPP_METRIC_PROPERTIES_METRIC_PROPERTIES_H_
