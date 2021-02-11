// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a fake 'smart door' component for security codelab.
// It CONTAINS vulnerability intentionally.
// DO NOT COPY ANY OF THE CODE IN THIS FILE!
#include "smart_door_server_app.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>

#include <fstream>
#include <vector>

#include "src/lib/digest/digest.h"

#define HASH_SIZE (digest::kSha256Length)
#define SALT_SIZE (16)
#define MAX_NAME_SIZE (32)
#define MAX_PASSPHRASE_SIZE (32)

namespace smart_door {

using fuchsia::security::codelabsmartdoor::Access_AddHomeMember_Response;
using fuchsia::security::codelabsmartdoor::Access_AddHomeMember_Result;
using fuchsia::security::codelabsmartdoor::Access_Open_Response;
using fuchsia::security::codelabsmartdoor::Access_Open_Result;
using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::MemorySyncPtr;
using fuchsia::security::codelabsmartdoor::Token;
using fuchsia::security::codelabsmartdoor::UserGroup;

SmartDoorServer::SmartDoorServer(MemorySyncPtr memory) {
  std::string id;
  readToken(&id);
  Token token;
  token.set_id(id);
  memory_client_.reset(new SmartDoorMemoryClientImpl(std::move(memory), std::move(token)));
}

bool SmartDoorServer::deserializeBuffer(const uint8_t* buffer, size_t buffer_size,
                                        std::vector<Passphrase>& passphrases) {
  const uint8_t* ptr = buffer;
  while (ptr < buffer + buffer_size) {
    uint8_t name_size = *(reinterpret_cast<const uint8_t*>(ptr));
    if (!name_size) {
      break;
    }
    const uint8_t* name_start = ptr + sizeof(uint8_t);
    const uint8_t* hash_start = name_start + name_size;
    const uint8_t* salt_start = hash_start + HASH_SIZE;
    passphrases.emplace_back(Passphrase{name_size, name_start, hash_start, salt_start});
    // We need to have one more space for '\0'.
    if (++name_size > MAX_NAME_SIZE) {
      FX_LOGS(WARNING) << "invalid name size!" << std::endl;
      return false;
    }
    ptr = salt_start + SALT_SIZE;
  }
  return true;
}

void SmartDoorServer::serializeBuffer(std::vector<uint8_t>& buffer,
                                      const std::vector<Passphrase>& passphrases) {
  for (auto it = passphrases.begin(); it != passphrases.end(); it++) {
    buffer.push_back(reinterpret_cast<uint8_t>(it->name_size));
    buffer.insert(buffer.end(), it->name_start, it->name_start + it->name_size);
    buffer.insert(buffer.end(), it->hash_start, it->hash_start + HASH_SIZE);
    buffer.insert(buffer.end(), it->salt_start, it->salt_start + SALT_SIZE);
  }
}

bool SmartDoorServer::readStringFromDocument(const rapidjson::Document& document, const char* name,
                                             std::string* out) {
  auto member = document.FindMember(name);
  if (member == document.MemberEnd()) {
    return false;
  }

  if (!member->value.IsString()) {
    return false;
  }

  const char* value = member->value.GetString();
  *out = std::string(value);
  return true;
}

bool SmartDoorServer::hexDecode(std::string s, uint8_t* out, size_t out_size) {
  const char* ptr = s.c_str();
  uint8_t* out_ptr = out;
  while (ptr < s.c_str() + s.size() && out_ptr < out + out_size) {
    unsigned int hexNum = 0;
    if (sscanf(ptr, "%02x", &hexNum) != 1) {
      break;
    }
    ZX_ASSERT(hexNum <= 0xFF);
    *out_ptr = static_cast<uint8_t>(hexNum);
    ptr += 2;
    out_ptr++;
  }
  if (out_ptr != out + out_size) {
    return false;
  }
  return true;
}

bool SmartDoorServer::readToken(std::string* token) {
  json::JSONParser json_parser;
  auto doc = json_parser.ParseFromFile("/config/data/config.json");
  if (json_parser.HasError()) {
    FX_LOGS(ERROR) << "failed to read from config file!" << std::endl;
    return false;
  }
  if (!readStringFromDocument(doc, "token_id", token)) {
    FX_LOGS(ERROR) << "failed to parse document!" << std::endl;
    return false;
  }
  return true;
}

bool SmartDoorServer::readAdminPassphrase(uint8_t* admin_hash, uint8_t* admin_salt) {
  json::JSONParser json_parser;
  auto doc = json_parser.ParseFromFile("/config/data/config.json");
  if (json_parser.HasError()) {
    FX_LOGS(ERROR) << "failed to read from config file!" << std::endl;
    return false;
  }
  std::string admin_hash_hex;
  std::string admin_salt_hex;
  if (!readStringFromDocument(doc, "admin_hash", &admin_hash_hex)) {
    FX_LOGS(ERROR) << "failed to parse document!" << std::endl;
    return false;
  }
  if (!readStringFromDocument(doc, "admin_salt", &admin_salt_hex)) {
    FX_LOGS(ERROR) << "failed to parse document!" << std::endl;
    return false;
  }
  if (!hexDecode(admin_hash_hex, admin_hash, HASH_SIZE)) {
    FX_LOGS(ERROR) << "failed to decode hex!" << std::endl;
    return false;
  }
  if (!hexDecode(admin_salt_hex, admin_salt, SALT_SIZE)) {
    FX_LOGS(ERROR) << "failed to decode hex!" << std::endl;
    return false;
  }
  return true;
}

bool SmartDoorServer::checkPassphrase(const uint8_t* passphrase, size_t passphrase_size,
                                      const uint8_t* hash, const uint8_t* salt) {
  if (passphrase_size > MAX_PASSPHRASE_SIZE) {
    return false;
  }
  digest::Digest d;
  d.Init();
  d.Update(passphrase, passphrase_size);
  d.Update(salt, SALT_SIZE);
  const uint8_t* expected_hash = d.Final();
  if (memcmp(hash, expected_hash, HASH_SIZE)) {
    if (debug_) {
      char passphrase_hex[MAX_PASSPHRASE_SIZE * 2 + 1] = {};
      for (size_t i = 0; i < passphrase_size; i++) {
        sprintf(&passphrase_hex[i * 2], "%02X", passphrase[i]);
      }
      FX_LOGS(INFO) << "passphrase mismatch, input passphrase: " << passphrase_hex << std::endl;
      char hash_hex[HASH_SIZE * 2 + 1] = {};
      for (size_t i = 0; i < HASH_SIZE; i++) {
        sprintf(&hash_hex[i * 2], "%02X", hash[i]);
      }
      FX_LOGS(INFO) << "expected hash: " << hash_hex << std::endl;
      char salt_hex[SALT_SIZE * 2 + 1] = {};
      for (size_t i = 0; i < SALT_SIZE; i++) {
        sprintf(&salt_hex[i * 2], "%02X", salt[i]);
      }
      FX_LOGS(INFO) << "expected salt: " << salt_hex << std::endl;
    }
    return false;
  }
  return true;
}

void SmartDoorServer::AddHomeMember(std::string name, std::vector<uint8_t> passphrase,
                                    AddHomeMemberCallback callback) {
  if (name.size() + 1 > MAX_NAME_SIZE) {
    callback(Access_AddHomeMember_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  if (passphrase.size() > MAX_PASSPHRASE_SIZE) {
    callback(Access_AddHomeMember_Result::WithErr(Error::INVALID_INPUT));
    return;
  }

  // read buffer from "smart-door-memory".
  std::vector<uint8_t> buffer;
  if (!memory_client_->read(buffer)) {
    callback(Access_AddHomeMember_Result::WithErr(Error::INTERNAL));
    return;
  }
  std::vector<Passphrase> passphrases;
  if (!deserializeBuffer(buffer.data(), buffer.size(), passphrases)) {
    callback(Access_AddHomeMember_Result::WithErr(Error::INTERNAL));
    return;
  }
  for (auto it = passphrases.begin(); it != passphrases.end(); it++) {
    if (it->name_size == name.size() && !memcmp(it->name_start, name.c_str(), it->name_size)) {
      callback(Access_AddHomeMember_Result::WithErr(Error::USER_EXISTS));
      return;
    }
  }

  // Generate a random salt.
  uint8_t salt[SALT_SIZE] = {};
  zx_cprng_draw(salt, SALT_SIZE);

  // Calculate the passphrase hash.
  digest::Digest d;
  d.Init();
  d.Update(passphrase.data(), passphrase.size());
  d.Update(salt, SALT_SIZE);
  passphrases.emplace_back(Passphrase{static_cast<uint8_t>(name.size()),
                                      reinterpret_cast<const uint8_t*>(name.c_str()), d.Final(),
                                      salt});

  // Serialize the passphrase information and write it to smart-door-memory.
  std::vector<uint8_t> new_buffer;
  serializeBuffer(new_buffer, passphrases);
  if (!memory_client_->write(new_buffer)) {
    callback(Access_AddHomeMember_Result::WithErr(Error::INTERNAL));
    return;
  }

  callback(Access_AddHomeMember_Result::WithResponse(Access_AddHomeMember_Response()));
  return;
}

void SmartDoorServer::Open(std::string name, std::vector<uint8_t> passphrase,
                           OpenCallback callback) {
  if (name.size() + 1 > MAX_NAME_SIZE) {
    callback(Access_Open_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  if (passphrase.size() > MAX_PASSPHRASE_SIZE) {
    callback(Access_Open_Result::WithErr(Error::INVALID_INPUT));
    return;
  }

  char welcome_message[256] = {};
  uint8_t admin_passphrase_hash[HASH_SIZE] = {};
  uint8_t admin_passphrase_salt[SALT_SIZE] = {};
  char known_name[MAX_NAME_SIZE] = {};
  uint8_t hash[HASH_SIZE] = {};
  uint8_t salt[SALT_SIZE] = {};

  // We use snprintf instead of sprintf to make sure we don't overflow the buffer!
  snprintf(welcome_message, sizeof(welcome_message), "welcome!! %s!!\n", name.c_str());

  if (!readAdminPassphrase(admin_passphrase_hash, admin_passphrase_salt)) {
    callback(Access_Open_Result::WithErr(Error::INTERNAL));
    return;
  }

  // read buffer from "smart-door-memory".
  // buffer format is | name_size | name | passphrase_hash | passphrase_salt |
  std::vector<uint8_t> buffer;
  if (!memory_client_->read(buffer)) {
    callback(Access_Open_Result::WithErr(Error::INTERNAL));
    return;
  }

  std::vector<Passphrase> passphrases;
  if (!deserializeBuffer(buffer.data(), buffer.size(), passphrases)) {
    callback(Access_Open_Result::WithErr(Error::INTERNAL));
    return;
  }

  for (auto it = passphrases.begin(); it != passphrases.end(); it++) {
    memcpy(known_name, it->name_start, it->name_size);
    memcpy(hash, it->hash_start, HASH_SIZE);
    memcpy(salt, it->salt_start, SALT_SIZE);
    if (!strcmp(known_name, name.c_str()) &&
        checkPassphrase(passphrase.data(), passphrase.size(), hash, salt)) {
      FX_LOGS(INFO) << welcome_message;
      callback(Access_Open_Result::WithResponse(Access_Open_Response(UserGroup::REGULAR)));
      return;
    }
  }

  // If none of the user passphrase matches, check for admin passphrase.
  if (checkPassphrase(passphrase.data(), passphrase.size(), admin_passphrase_hash,
                      admin_passphrase_salt)) {
    FX_LOGS(INFO) << "welcome admin!!" << std::endl;
    callback(Access_Open_Result::WithResponse(Access_Open_Response(UserGroup::ADMIN)));
    return;
  }

  FX_LOGS(WARNING) << "wrong passphrase!!" << std::endl;
  callback(Access_Open_Result::WithErr(Error::WRONG_PASSPHRASE));
  return;
}

void SmartDoorServer::SetDebugFlag(bool enable, SetDebugFlagCallback callback) {
  debug_ = enable;
  callback();
}

void SmartDoorServer::Reset(ResetCallback callback) {
  // The only state we maintain in SmartDoor is the debug_ field.
  debug_ = false;
  callback();
}

SmartDoorServerApp::SmartDoorServerApp()
    : SmartDoorServerApp(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

SmartDoorServerApp::SmartDoorServerApp(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {
  MemorySyncPtr memory;
  context_->svc()->Connect(memory.NewRequest());
  service_.reset(new SmartDoorServer(std::move(memory)));
  context_->outgoing()->AddPublicService(bindings_.GetHandler(service_.get()));
}

SmartDoorServerApp::SmartDoorServerApp(std::unique_ptr<sys::ComponentContext> context,
                                       std::shared_ptr<SmartDoorMemoryClient> client)
    : service_(new SmartDoorServer(std::move(client))), context_(std::move(context)) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(service_.get()));
  context_->outgoing()->AddPublicService(reset_bindings_.GetHandler(service_.get()));
}

}  // namespace smart_door
