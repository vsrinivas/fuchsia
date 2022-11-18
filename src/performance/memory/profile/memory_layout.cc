// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/memory/profile/memory_layout.h"

#include <iomanip>
#include <sstream>

template <class T>
void ReadValue(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

void ReadValue(std::istream& is, bool& value) {
  uint8_t v = 0;
  ReadValue(is, v);
  value = v;
}

void BuildId::Read(std::istream& is) {
  uint64_t size = 0;
  ReadValue(is, size);
  for (uint64_t i = 0; i < size; i++) {
    ReadValue(is, value.emplace_back());
  }
}

std::string BuildId::ToHex() {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (auto v : value) {
    ss << std::setw(2) << static_cast<unsigned>(v);
  }
  return ss.str();
}

void Mmap::Read(std::istream& is) {
  ReadValue(is, starting_address);
  ReadValue(is, size);
  ReadValue(is, module_index);
  ReadValue(is, readable);
  ReadValue(is, writable);
  ReadValue(is, executable);
  ReadValue(is, relative_addr);
}

void Layout::Read(std::istream& is) {
  while (true) {
    switch (is.peek()) {
      case 'o':
        is.get();
        modules.emplace_back().Read(is);
        break;
      case 'm':
        is.get();
        mmaps.emplace_back().Read(is);
        break;
      default:
        return;
    }
  }
}
