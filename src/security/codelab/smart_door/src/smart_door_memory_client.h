// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_SECURITY_CODELAB_SMART_DOOR_SRC_SMART_DOOR_MEMORY_CLIENT_H_
#define SRC_SECURITY_CODELAB_SMART_DOOR_SRC_SMART_DOOR_MEMORY_CLIENT_H_

#include <fuchsia/security/codelabsmartdoor/cpp/fidl.h>

#include <vector>

namespace smart_door {

class SmartDoorMemoryClient {
 public:
  virtual bool write(const std::vector<uint8_t>& buffer) = 0;
  virtual bool read(std::vector<uint8_t>& buffer) = 0;
  virtual ~SmartDoorMemoryClient() = default;
};

class SmartDoorMemoryClientImpl : public SmartDoorMemoryClient {
 public:
  SmartDoorMemoryClientImpl(fuchsia::security::codelabsmartdoor::MemorySyncPtr memory,
                            fuchsia::security::codelabsmartdoor::Token token)
      : memory_(std::move(memory)), token_(std::move(token)) {}
  bool write(const std::vector<uint8_t>& buffer) override;
  bool read(std::vector<uint8_t>& buffer) override;
  ~SmartDoorMemoryClientImpl() {}

 private:
  fuchsia::security::codelabsmartdoor::MemorySyncPtr memory_;
  fuchsia::security::codelabsmartdoor::Token token_;
};

}  // namespace smart_door

#endif  // SRC_SECURITY_CODELAB_SMART_DOOR_SRC_SMART_DOOR_MEMORY_CLIENT_H_
