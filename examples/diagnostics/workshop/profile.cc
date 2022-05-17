// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile.h"

#include <lib/async/dispatcher.h>

#include <string>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/lib/files/file.h"
#include "src/lib/json_parser/json_parser.h"

bool LoadFromFile(std::string& filepath, std::string* name, int64_t* balance) {
  json::JSONParser json_parser;
  rapidjson::Document document = json_parser.ParseFromFile(filepath);
  if (json_parser.HasError()) {
    return false;
  }
  *name = document["name"].GetString();
  *balance = document["balance"].GetInt();
  return true;
}

bool SaveToFile(std::string& filepath, std::string& name, int64_t balance) {
  rapidjson::Document document;
  document.SetObject();
  document.AddMember("name", name, document.GetAllocator());
  document.AddMember("balance", balance, document.GetAllocator());
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return files::WriteFile(filepath, buffer.GetString());
}

Profile::Profile(async_dispatcher_t* dispatcher, std::string filepath)
    : balance_(0), filepath_(filepath), reader_(this), dispatcher_(dispatcher) {
  LoadFromFile(filepath_, &name_, &balance_);

  bindings_.set_empty_set_handler([&]() { SaveToFile(filepath_, name_, balance_); });
}

Profile::~Profile() = default;

void Profile::SetName(std::string name) { name_ = std::move(name); }

void Profile::GetName(GetNameCallback callback) { callback(name_); }

void Profile::AddBalance(int64_t amount) { balance_ += amount; }

void Profile::WithdrawBalance(int64_t amount, WithdrawBalanceCallback callback) {
  if (balance_ >= amount) {
    balance_ -= amount;
    callback(true);
    return;
  }
  callback(false);
}

void Profile::GetBalance(GetBalanceCallback callback) { callback(balance_); }

Profile::Reader::Reader(Profile* parent) : parent_(parent) {}

Profile::Reader::~Reader() = default;

void Profile::Reader::GetName(GetNameCallback callback) { parent_->GetName(std::move(callback)); }

void Profile::Reader::GetBalance(GetBalanceCallback callback) {
  parent_->GetBalance(std::move(callback));
}
