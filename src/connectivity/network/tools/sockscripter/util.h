// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_UTIL_H_

#include <string>

bool str2int(const std::string& in_str, int* out_int);
bool getFlagInt(const std::string& in_str, int* out_int);
const char* GetDomainName(int domain);
const char* GetTypeName(int type);

#endif  // SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_UTIL_H_
