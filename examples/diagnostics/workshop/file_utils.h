// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_WORKSHOP_FILE_UTILS_H_
#define EXAMPLES_DIAGNOSTICS_WORKSHOP_FILE_UTILS_H_

#include <cstdint>
#include <string>

std::string FilepathForKey(const std::string& key);

bool LoadFromFile(const std::string& filepath, std::string* name, int64_t* balance);

bool SaveToFile(const std::string& filepath, const std::string& name, int64_t balance);

#endif  // EXAMPLES_DIAGNOSTICS_WORKSHOP_FILE_UTILS_H_
