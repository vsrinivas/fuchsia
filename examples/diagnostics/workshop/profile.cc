// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile.h"

#include <lib/async/dispatcher.h>

#include <string>

#include "file_utils.h"

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
