// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_FILE_SYNC_UTIL_H_
#define SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_FILE_SYNC_UTIL_H_

#include <lib/zx/socket.h>
#include <sys/stat.h>

#include <string>
#include <vector>

std::vector<std::string> split_string(const std::string& str, const std::string& deliminator);
bool match(const std::vector<std::string>& parts0, const std::vector<std::string>& parts1);
std::string ConcatenateRelativePath(std::vector<std::string>::const_iterator begin,
                                    std::vector<std::string>::const_iterator end,
                                    const std::string& deliminator = "/");
std::string ConcatenateRelativePath(const std::vector<std::string>& str,
                                    const std::string& deliminator = "/");
bool ReadFdExactly(zx::socket& socket, void* buf, size_t len);
bool WriteFdExactly(zx::socket& socket, const void* buf, size_t len);

#endif  // SRC_DEVELOPER_ADB_THIRD_PARTY_ADB_FILE_SYNC_UTIL_H_
