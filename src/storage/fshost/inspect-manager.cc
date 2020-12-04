// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect-manager.h"

#include <lib/inspect/service/cpp/service.h>
#include <sys/stat.h>

#include <fs/service.h>

namespace fio = ::llcpp::fuchsia::io;

namespace devmgr {

zx_status_t OpenNode(zx::unowned_channel root, const std::string& path, uint32_t mode,
                     zx::channel* result) {
  zx::channel dir_chan, server;
  zx_status_t status = zx::channel::create(0, &dir_chan, &server);
  if (status != ZX_OK) {
    return status;
  }

  fidl::StringView path_view(fidl::unowned_ptr(&path[0]), strlen(&path[0]));
  status = fio::Directory::Call::Open(std::move(root),
                                      fs::VnodeConnectionOptions::ReadOnly().ToIoV1Flags(), mode,
                                      std::move(path_view), std::move(server))
               .status();
  if (status != ZX_OK) {
    return status;
  }
  *result = std::move(dir_chan);
  return ZX_OK;
}

InspectManager::InspectManager() = default;
InspectManager::~InspectManager() = default;

fbl::RefPtr<fs::PseudoDir> InspectManager::Initialize(async_dispatcher* dispatcher) {
  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  diagnostics_dir->AddEntry(
      fuchsia::inspect::Tree::Name_,
      fbl::MakeRefCounted<fs::Service>([connector = inspect::MakeTreeHandler(
                                            &inspector_, dispatcher)](zx::channel chan) mutable {
        connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
        return ZX_OK;
      }));
  return diagnostics_dir;
}

void InspectManager::ServeStats(const std::string& name, fbl::RefPtr<fs::Vnode> root) {
  inspector_.GetRoot().CreateLazyNode(
      name + "_stats",
      [this, name = std::move(name), root = std::move(root)] {
        inspect::Inspector insp;
        zx::channel root_chan;
        zx_status_t status =
            devmgr::OpenNode(zx::unowned_channel(root->GetRemote()), "/", S_IFDIR, &root_chan);
        if (status != ZX_OK) {
          return fit::make_result_promise(fit::ok(std::move(insp)));
        }
        FillFileTreeSizes(std::move(root_chan), insp.GetRoot().CreateChild(name), &insp);
        FillStats(zx::unowned_channel(root->GetRemote()), &insp);
        return fit::make_result_promise(fit::ok(std::move(insp)));
      },
      &inspector_);
}

void InspectManager::FillStats(zx::unowned_channel dir_chan, inspect::Inspector* inspector) {
  auto result = fio::DirectoryAdmin::Call::QueryFilesystem(std::move(dir_chan));
  inspect::Node stats = inspector->GetRoot().CreateChild("stats");
  if (result.status() == ZX_OK) {
    fio::DirectoryAdmin::QueryFilesystemResponse* response = result.Unwrap();
    fio::FilesystemInfo* info = response->info.get();
    if (info != nullptr) {
      stats.CreateUint("total_bytes", info->total_bytes + info->free_shared_pool_bytes, inspector);
      stats.CreateUint("used_bytes", info->used_bytes, inspector);
    } else {
      stats.CreateString("error", "Query failed", inspector);
    }
  } else {
    stats.CreateString("error", "Query failed", inspector);
  }
  inspector->emplace(std::move(stats));
}

void InspectManager::FillFileTreeSizes(zx::channel current_dir, inspect::Node node,
                                       inspect::Inspector* inspector) {
  struct PendingDirectory {
    std::unique_ptr<DirectoryEntriesIterator> entries_iterator;
    inspect::Node node;
    size_t total_size;
  };

  // Keeps track of entries in the stack, the entry at N+1 will always be a child of the entry at N
  // to be able to update the parent `total_size` and propaget the sizes up. We use the lazy
  // iterator to have a single child connection at a time per node.
  std::vector<PendingDirectory> work_stack;
  auto current = PendingDirectory{
      .entries_iterator = std::make_unique<DirectoryEntriesIterator>(std::move(current_dir)),
      .node = std::move(node),
      .total_size = 0,
  };
  work_stack.push_back(std::move(current));

  while (!work_stack.empty()) {
    auto& current = work_stack.back();

    // If we have finished with this node then pop it from the stack, save it in inspect and
    // continue.
    if (current.entries_iterator->finished()) {
      // Maintain this node alive in inspect by adding it to the inspector value list and delete the
      // stack item.
      current.node.CreateUint("size", current.total_size, inspector);
      inspector->emplace(std::move(current.node));
      size_t size = current.total_size;

      work_stack.pop_back();

      // The next node in the stack is the parent of this node. Increment its size by the total size
      // of this node. If the work stack is emtpy, then `current` is the root.
      if (!work_stack.empty()) {
        work_stack.back().total_size += size;
      }

      continue;
    }

    // Get the next entry.
    while (auto entry = current.entries_iterator->GetNext()) {
      // If the entry is a directory, push it to the stack and continue the stack loop.
      if (entry->is_dir) {
        work_stack.push_back(PendingDirectory{
            .entries_iterator = std::make_unique<DirectoryEntriesIterator>(std::move(entry->node)),
            .node = current.node.CreateChild(entry->name),
            .total_size = 0,
        });
        break;
      } else {
        // If the entry is a file, record its size.
        inspect::Node child_node = current.node.CreateChild(entry->name);
        child_node.CreateUint("size", entry->size, inspector);
        inspector->emplace(std::move(child_node));
        current.total_size += entry->size;
      }
    }
  }
}

// Create a new lazy iterator.
DirectoryEntriesIterator::DirectoryEntriesIterator(zx::channel directory)
    : directory_(std::move(directory)), finished_(false) {}

// Get the next entry. If there's no more entries left, this method will return std::nullopt
// forever.
std::optional<DirectoryEntry> DirectoryEntriesIterator::GetNext() {
  // Loop until we can return an entry or there are none left.
  while (true) {
    // If we have pending entries to return, take one and return it. If for some reason, we fail
    // to make a result out of the pending entry (it may not exist anymore) then keep trying until
    // we can return one.
    while (!pending_entries_.empty()) {
      auto entry_name = pending_entries_.front();
      pending_entries_.pop();
      if (auto result = MaybeMakeEntry(entry_name)) {
        return result;
      }
    }

    // When there are no pending entries and we have already finished, return.
    if (finished_) {
      return std::nullopt;
    }

    // Load the next set of dirents.
    RefreshPendingEntries();

    // If we didn't find any pending entries in this batch of dirents, then we have finished.
    if (pending_entries_.empty()) {
      finished_ = true;
      return std::nullopt;
    }
  }
}

std::optional<DirectoryEntry> DirectoryEntriesIterator::MaybeMakeEntry(
    const std::string& entry_name) {
  // Open child of the current node with the given entry name.
  zx::channel child_chan;
  zx_status_t status =
      OpenNode(zx::unowned_channel(directory_), entry_name, S_IFREG | S_IFDIR, &child_chan);
  if (status != ZX_OK) {
    return std::nullopt;
  }

  // Get child attributes to know whether the child is a directory or not.
  auto result = fio::Directory::Call::GetAttr(zx::unowned_channel(child_chan));
  if (result.status() != ZX_OK) {
    return std::nullopt;
  }
  fio::Directory::GetAttrResponse* response = result.Unwrap();

  bool is_dir = response->attributes.mode & fio::MODE_TYPE_DIRECTORY;
  return std::optional<DirectoryEntry>{{
      .name = entry_name,
      .node = std::move(child_chan),
      .size = (is_dir) ? 0 : response->attributes.content_size,
      .is_dir = is_dir,
  }};
}

// Reads the next set of dirents and loads them into `pending_entries_`.
void DirectoryEntriesIterator::RefreshPendingEntries() {
  auto result = fio::Directory::Call::ReadDirents(zx::unowned_channel(directory_), fio::MAX_BUF);
  if (result.status() != ZX_OK) {
    return;
  }
  fio::Directory::ReadDirentsResponse* response = result.Unwrap();
  if (response->dirents.count() == 0) {
    return;
  }

  size_t offset = 0;
  auto data_ptr = response->dirents.data();

  while (sizeof(vdirent_t) < response->dirents.count() - offset) {
    const vdirent_t* entry = reinterpret_cast<const vdirent_t*>(data_ptr + offset);
    std::string entry_name(entry->name, entry->size);
    offset += sizeof(vdirent_t) + entry->size;
    if (entry_name == "." || entry_name == "..") {
      continue;
    }
    pending_entries_.push(std::move(entry_name));
  }
}

}  // namespace devmgr
