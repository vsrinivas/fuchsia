// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <fidl/fuchsia.io/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

std::vector<std::string> split_string(const std::string& str, const std::string& deliminator) {
  size_t pos;
  std::vector<std::string> parts;
  std::string tmp_str = str;
  while ((pos = tmp_str.find(deliminator)) != std::string::npos) {
    auto substr = tmp_str.substr(0, pos);
    if (substr.length() && substr != ".") {  // Remove empty strings and "." (current directory)
      parts.push_back(substr);
    }
    tmp_str.erase(0, pos + deliminator.length());
  }
  if (tmp_str.length() && tmp_str != ".") {  // Remove empty strings and "." (current directory)
    parts.push_back(tmp_str);
  }

  return parts;
}

bool match(const std::vector<std::string>& parts0, const std::vector<std::string>& parts1) {
  if (parts0.size() != parts1.size()) {
    return false;
  }

  for (uint32_t i = 0; i < parts0.size(); i++) {
    if (parts0[i].substr(0, parts0[i].find(':')) != parts1[i].substr(0, parts1[i].find(':'))) {
      return false;
    }
  }

  return true;
}

std::string ConcatenateRelativePath(std::vector<std::string>::const_iterator begin,
                                    std::vector<std::string>::const_iterator end,
                                    const std::string& deliminator) {
  std::string ret;
  std::for_each(begin, end, [&ret](const std::string& s) { ret.append(s + "/"); });
  return ret.erase(ret.size() - 1);
}

std::string ConcatenateRelativePath(const std::vector<std::string>& str,
                                    const std::string& deliminator) {
  return ConcatenateRelativePath(str.begin(), str.end(), deliminator);
}

std::string dump_hex(const void* data, size_t byte_count) {
  size_t truncate_len = 16;
  bool truncated = false;
  if (byte_count > truncate_len) {
    byte_count = truncate_len;
    truncated = true;
  }

  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);

  std::string line;
  for (size_t i = 0; i < byte_count; ++i) {
    if ((i % 4) == 0) {
      line += " 0x";
    }
    char byte[32];
    sprintf(byte, "%02x", p[i]);
    line += byte;
  }
  line.push_back(' ');

  for (size_t i = 0; i < byte_count; ++i) {
    uint8_t ch = p[i];
    line.push_back(isprint(ch) ? ch : '.');
  }

  if (truncated) {
    line += " [truncated]";
  }

  return line;
}

bool ReadFdExactly(zx::socket& socket, void* buf, size_t len) {
  char* p = reinterpret_cast<char*>(buf);
  size_t len0 = len;
  FX_LOGS(DEBUG) << "readx: wanted=" << len;
  while (len > 0) {
    size_t actual;
    auto status =
        socket.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      FX_LOGS(DEBUG) << "Socket wait failed " << status;
      return false;
    }
    status = socket.read(0, p, len, &actual);
    if (status != ZX_OK) {
      FX_LOGS(DEBUG) << "Socket read failed " << status;
      return false;
    }
    len -= actual;
    p += actual;
  }
  FX_LOGS(DEBUG) << "readx: wanted=" << len0 << " got=" << (len0 - len) << " "
                 << dump_hex(reinterpret_cast<const unsigned char*>(buf), len0);
  return true;
}

bool WriteFdExactly(zx::socket& socket, const void* buf, size_t len) {
  const char* p = reinterpret_cast<const char*>(buf);
  FX_LOGS(DEBUG) << "writex: len=" << len << " "
                 << dump_hex(reinterpret_cast<const unsigned char*>(buf), len);
  while (len > 0) {
    size_t actual;
    auto status =
        socket.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      FX_LOGS(DEBUG) << "Socket wait failed " << status;
      return false;
    }
    status = socket.write(0, p, len, &actual);
    if (status != ZX_OK) {
      FX_LOGS(DEBUG) << "Socket write failed " << status;
      return false;
    }
    len -= actual;
    p += actual;
  }
  return true;
}
