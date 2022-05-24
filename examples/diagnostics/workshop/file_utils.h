// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_WORKSHOP_FILE_UTILS_H_
#define EXAMPLES_DIAGNOSTICS_WORKSHOP_FILE_UTILS_H_

std::string FilepathForKey(std::string& key);

bool LoadFromFile(std::string& filepath, std::string* name, int64_t* balance);

bool SaveToFile(std::string& filepath, std::string& name, int64_t balance);

#endif  // EXAMPLES_DIAGNOSTICS_WORKSHOP_FILE_UTILS_H_
