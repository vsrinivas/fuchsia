// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/pseudo_dir.h>

#include <mutex>

namespace vfs {

PseudoDir::PseudoDir() = default;

PseudoDir::~PseudoDir() = default;

zx_status_t PseudoDir::AddSharedEntry(std::string name,
                                      std::shared_ptr<Node> vn) {
  ZX_DEBUG_ASSERT(vn);

  auto id = next_node_id_++;
  return AddEntry(
      std::make_unique<SharedEntry>(id, std::move(name), std::move(vn)));
}

zx_status_t PseudoDir::AddEntry(std::string name, std::unique_ptr<Node> vn) {
  ZX_DEBUG_ASSERT(vn);

  auto id = next_node_id_++;
  return AddEntry(
      std::make_unique<UniqueEntry>(id, std::move(name), std::move(vn)));
}

zx_status_t PseudoDir::AddEntry(std::unique_ptr<Entry> entry) {
  ZX_DEBUG_ASSERT(entry);

  if (!IsValidName(entry->name())) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> guard(mutex_);

  if (entries_by_name_.find(entry->name()) != entries_by_name_.end()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  entries_by_name_[entry->name()] = entry.get();
  auto id = entry->id();
  entries_by_id_.emplace_hint(entries_by_id_.end(), id, std::move(entry));

  return ZX_OK;
}

zx_status_t PseudoDir::RemoveEntry(const std::string& name) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto entry = entries_by_name_.find(name);
  if (entry == entries_by_name_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  entries_by_id_.erase(entry->second->id());
  entries_by_name_.erase(name);

  return ZX_OK;
}

zx_status_t PseudoDir::Lookup(const std::string& name, Node** out_node) const {
  std::lock_guard<std::mutex> guard(mutex_);

  auto search = entries_by_name_.find(name);
  if (search != entries_by_name_.end()) {
    *out_node = search->second->node();
    return ZX_OK;
  } else {
    return ZX_ERR_NOT_FOUND;
  }
}

bool PseudoDir::IsEmpty() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return entries_by_name_.size() == 0;
}

PseudoDir::Entry::Entry(uint64_t id, std::string name)
    : id_(id), name_(std::move(name)) {}

PseudoDir::Entry::~Entry() = default;

PseudoDir::SharedEntry::SharedEntry(uint64_t id, std::string name,
                                    std::shared_ptr<Node> node)
    : Entry(id, std::move(name)), node_(std::move(node)) {}

Node* PseudoDir::SharedEntry::node() const { return node_.get(); }

PseudoDir::SharedEntry::~SharedEntry() = default;

PseudoDir::UniqueEntry::UniqueEntry(uint64_t id, std::string name,
                                    std::unique_ptr<Node> node)
    : Entry(id, std::move(name)), node_(std::move(node)) {}

Node* PseudoDir::UniqueEntry::node() const { return node_.get(); }

PseudoDir::UniqueEntry::~UniqueEntry() = default;

}  // namespace vfs
