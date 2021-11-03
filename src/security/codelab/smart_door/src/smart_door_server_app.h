// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a fake 'smart door' component for security codelab.
// It CONTAINS vulnerability intentionally.
// DO NOT COPY ANY OF THE CODE IN THIS FILE!
#ifndef SRC_SECURITY_CODELAB_SMART_DOOR_SRC_SMART_DOOR_SERVER_APP_H_
#define SRC_SECURITY_CODELAB_SMART_DOOR_SRC_SMART_DOOR_SERVER_APP_H_

#include <fuchsia/security/codelabsmartdoor/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <src/lib/json_parser/json_parser.h>

#include "smart_door_memory_client.h"

namespace smart_door {

class SmartDoorServer : public fuchsia::security::codelabsmartdoor::Access,
                        public fuchsia::security::codelabsmartdoor::AccessReset {
 public:
  SmartDoorServer(std::shared_ptr<SmartDoorMemoryClient> memory_client)
      : memory_client_(std::move(memory_client)) {}
  SmartDoorServer(fuchsia::security::codelabsmartdoor::MemorySyncPtr memory);
  virtual void AddHomeMember(std::string user_name, std::vector<uint8_t> passphrase,
                             AddHomeMemberCallback callback);
  virtual void Open(std::string user_name, std::vector<uint8_t> passphrase, OpenCallback callback);
  virtual void Close() {
    // Do nothing.
  }
  virtual void SetDebugFlag(bool enable, SetDebugFlagCallback callback);
  virtual void Reset(ResetCallback callback);

 private:
  // A simple structure for storing one entry of name, passphrase hash, passphrase salt.
  // This structure is serialized as | name_size | name | passphrase_hash | passphrase_salt |.
  struct Passphrase {
    uint8_t name_size;
    const uint8_t* name_start;
    const uint8_t* hash_start;
    const uint8_t* salt_start;
  };

  bool checkPassphrase(const uint8_t* passphrase, size_t passphrase_size, const uint8_t* hash,
                       const uint8_t* salt);
  static bool readToken(std::string* token);
  static bool readAdminPassphrase(uint8_t* admin_hash, uint8_t* admin_salt);
  static bool deserializeBuffer(const uint8_t* buffer, size_t buffer_size,
                                std::vector<Passphrase>& passphrases);
  static void serializeBuffer(std::vector<uint8_t>& buffer,
                              const std::vector<Passphrase>& passphrases);
  static bool hexDecode(std::string s, uint8_t* out, size_t out_size);
  static bool readStringFromDocument(const rapidjson::Document& document, const char* name,
                                     std::string* out);

  std::shared_ptr<SmartDoorMemoryClient> memory_client_;
  bool debug_ = false;
};

class SmartDoorServerApp {
 public:
  explicit SmartDoorServerApp();

 protected:
  SmartDoorServerApp(std::unique_ptr<sys::ComponentContext> context);
  // For test only.
  SmartDoorServerApp(std::unique_ptr<sys::ComponentContext> context,
                     std::shared_ptr<SmartDoorMemoryClient> client);

 private:
  using Access = fuchsia::security::codelabsmartdoor::Access;
  using AccessReset = fuchsia::security::codelabsmartdoor::AccessReset;
  SmartDoorServerApp(const SmartDoorServerApp&) = delete;
  SmartDoorServerApp& operator=(const SmartDoorServerApp&) = delete;

  std::unique_ptr<SmartDoorServer> service_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Access> bindings_;
  fidl::BindingSet<AccessReset> reset_bindings_;
};

}  // namespace smart_door
#endif  // SRC_SECURITY_CODELAB_SMART_DOOR_SRC_SMART_DOOR_SERVER_APP_H_
