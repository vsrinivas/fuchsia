// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/message_queue/persistent_queue.h"

#include <utility>

#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>

#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

PersistentQueue::PersistentQueue(std::string file_name)
    : file_name_(std::move(file_name)) {
  std::string contents;
  if (files::ReadFileToString(file_name_, &contents)) {
    rapidjson::Document document;
    document.Parse(contents);
    if (!document.IsArray()) {
      FXL_LOG(ERROR) << "Expected " << file_name_ << " to contain a JSON array";
      return;
    }
    for (rapidjson::Value::ConstValueIterator it = document.Begin();
         it != document.End(); ++it) {
      if (!it->IsString()) {
        FXL_LOG(ERROR) << "Expected a string but got: " << it;
        continue;
      }
      queue_.emplace_back(it->GetString(), it->GetStringLength());
    }
  }
}

void PersistentQueue::Save() {
  rapidjson::Document document;
  document.SetArray();
  for (const auto& it : queue_) {
    rapidjson::Value value;
    value.SetString(it.data(), it.size());
    document.PushBack(value, document.GetAllocator());
  }
  std::string contents = JsonValueToString(document);
  if (!files::WriteFile(file_name_, contents.data(), contents.size())) {
    FXL_LOG(ERROR) << "Failed to write to: " << file_name_;
  }
}

}  // namespace modular
