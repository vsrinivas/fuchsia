// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data-provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <algorithm>

#include <fbl/string_piece.h>

namespace fuzzing {

// Public methods

DataProviderImpl::DataProviderImpl() : max_label_length_(0) {}

DataProviderImpl::~DataProviderImpl() {}

zx_status_t DataProviderImpl::Initialize(zx::vmo *out) {
  std::lock_guard<std::mutex> lock(lock_);
  if (inputs_.find("") != inputs_.end()) {
    FX_LOGS(ERROR) << "Initialize() called more than once";
    return ZX_ERR_BAD_STATE;
  }

  TestInput *input = &inputs_[""];
  zx_status_t status;
  if ((status = input->Create()) != ZX_OK || (status = input->Share(out)) != ZX_OK ||
      (status = input->vmo().signal(kInIteration, kBetweenIterations)) != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create the shared test input memory: "
                   << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

void DataProviderImpl::AddConsumerLabel(std::string label) {
  std::lock_guard<std::mutex> lock(lock_);
  max_label_length_ = std::max(max_label_length_, label.size());

  // Default construct a TestInput for the given label.
  inputs_[label];
}

void DataProviderImpl::AddConsumer(std::string label, zx::vmo vmo, AddConsumerCallback callback) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto input = inputs_.find(label);
    if (input == inputs_.end()) {
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }
    input->second.Link(vmo);
    input->second.vmo().signal(kInIteration, kBetweenIterations);
  }
  callback(ZX_OK);
}

zx_status_t DataProviderImpl::PartitionTestInput(const void *data, size_t size) {
  std::lock_guard<std::mutex> lock(lock_);
  if (inputs_.size() == 0) {
    FX_LOGS(ERROR) << "not initialized";
    return ZX_ERR_BAD_STATE;
  }
  for (auto &i : inputs_) {
    i.second.Clear();
  }
  if (!data || size == 0) {
    return ZX_OK;
  }
  const uint8_t *u8 = static_cast<const uint8_t *>(data);
  TestInput *input = &inputs_[""];
  size_t start = 0;
  for (size_t i = 0; i + 3 < size; ++i) {
    if (u8[i] != '#') {
      continue;
    }
    size_t len = i - start;
    ++i;
    if (u8[i] == '#') {
      input->Write(&u8[start], i - start);
      start = i + 1;
      continue;
    }
    if (u8[i] != '[') {
      continue;
    }
    ++i;
    size_t max = std::min(size, i + max_label_length_ + 1);
    for (size_t j = i; j < max; ++j) {
      if (u8[j] != ']') {
        continue;
      }
      std::string_view label(reinterpret_cast<const char *>(&u8[i]), j - i);
      i = j;
      auto iter = inputs_.find(label);
      if (iter != inputs_.end()) {
        input->Write(&u8[start], len);
        input = &iter->second;
        start = i + 1;
      }
      break;
    }
  }
  if (start < size) {
    input->Write(&u8[start], size - start);
  }
  for (auto &[label, input] : inputs_) {
    if (!input.is_mapped()) {
      continue;
    }
    zx_status_t status = input.vmo().signal(kBetweenIterations, kInIteration);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t DataProviderImpl::CompleteIteration() {
  std::lock_guard<std::mutex> lock(lock_);
  for (auto &[label, input] : inputs_) {
    if (!input.is_mapped()) {
      continue;
    }
    zx_status_t status = input.vmo().signal(kInIteration, kBetweenIterations);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

void DataProviderImpl::Reset() {
  std::lock_guard<std::mutex> lock(lock_);
  inputs_.clear();
}

// Protected metohds

bool DataProviderImpl::HasLabel(const std::string &label) {
  std::lock_guard<std::mutex> lock(lock_);
  return inputs_.find(label) != inputs_.end();
}

bool DataProviderImpl::IsMapped(const std::string &label) {
  std::lock_guard<std::mutex> lock(lock_);
  auto input = inputs_.find(label);
  return input != inputs_.end() && input->second.data() != nullptr;
}

}  // namespace fuzzing
