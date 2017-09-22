// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/farfs/file_system.h"

#include <fcntl.h>

#include "lib/fsl/tasks/message_loop.h"

namespace archive {
namespace {

struct DirRecord {
  DirRecord() = default;
  DirRecord(DirRecord&& other)
      : names(std::move(other.names)), children(std::move(other.children)) {}
  DirRecord(const DirRecord& other) = delete;

  void swap(DirRecord& other) {
    names.swap(other.names);
    children.swap(other.children);
  }

  fbl::RefPtr<vmofs::VnodeDir> CreateDirectory() {
    size_t count = names.size();
    fbl::Array<fbl::StringPiece> names_array(new fbl::StringPiece[count],
                                             count);
    fbl::Array<fbl::RefPtr<vmofs::Vnode>> children_array(
        new fbl::RefPtr<vmofs::Vnode>[count], count);
    for (size_t i = 0; i < count; ++i) {
      names_array[i] = names[i];
      children_array[i] = std::move(children[i]);
    }

    return fbl::AdoptRef(
        new vmofs::VnodeDir(std::move(names_array), std::move(children_array)));
  }

  std::vector<fbl::StringPiece> names;
  std::vector<fbl::RefPtr<vmofs::Vnode>> children;
};

fxl::StringView PopLastDirectory(fxl::StringView* path) {
  FXL_DCHECK(path->size() >= 2);  // Shortest path: "x/".
  size_t end = path->size() - 1;
  FXL_DCHECK(path->at(end) == '/');  // Must end with '/'.
  size_t begin = path->rfind('/', end - 1);
  if (begin == fxl::StringView::npos) {
    fxl::StringView result = path->substr(0, end);
    *path = fxl::StringView();
    return result;
  }
  fxl::StringView result = path->substr(begin + 1, end - begin - 1);
  FXL_DCHECK(!result.empty());  // "//" is disallowed.
  *path = path->substr(0, begin + 1);
  return result;
}

bool PopFirstDirectory(fxl::StringView* path) {
  size_t end = path->find('/');
  if (end == fxl::StringView::npos)
    return false;
  *path = path->substr(end + 1);
  return true;
}

fbl::StringPiece ToStringPiece(fxl::StringView view) {
  return fbl::StringPiece(view.data(), view.size());
}

fbl::RefPtr<vmofs::VnodeFile> CreateFile(zx_handle_t vmo,
                                         const DirectoryTableEntry& entry) {
  return fbl::AdoptRef(
      new vmofs::VnodeFile(vmo, entry.data_offset, entry.data_length));
}

void LeaveDirectory(fxl::StringView name, std::vector<DirRecord>* stack) {
  auto child = stack->back().CreateDirectory();
  stack->pop_back();
  DirRecord& parent = stack->back();
  parent.names.push_back(ToStringPiece(name));
  parent.children.push_back(std::move(child));
}

}  // namespace

FileSystem::FileSystem(zx::vmo vmo)
    : vmo_(vmo.get()), vfs_(fsl::MessageLoop::GetCurrent()->async()) {
  uint64_t num_bytes = 0;
  zx_status_t status = vmo.get_size(&num_bytes);
  if (status != ZX_OK)
    return;
  fxl::UniqueFD fd(fdio_vmo_fd(vmo.release(), 0, num_bytes));
  if (!fd.is_valid())
    return;
  reader_ = std::make_unique<ArchiveReader>(std::move(fd));
  CreateDirectory();
}

FileSystem::~FileSystem() = default;

bool FileSystem::Serve(zx::channel channel) {
  return directory_ &&
         vfs_.ServeDirectory(directory_, std::move(channel)) == ZX_OK;
}

zx::channel FileSystem::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return zx::channel();
  if (!Serve(std::move(h1)))
    return zx::channel();
  return h2;
}

zx::vmo FileSystem::GetFileAsVMO(fxl::StringView path) {
  if (!reader_)
    return zx::vmo();
  DirectoryTableEntry entry;
  if (!reader_->GetDirectoryEntryByPath(path, &entry))
    return zx::vmo();
  zx_handle_t result = ZX_HANDLE_INVALID;
  zx_vmo_clone(vmo_, ZX_VMO_CLONE_COPY_ON_WRITE, entry.data_offset,
               entry.data_length, &result);
  return zx::vmo(result);
}

bool FileSystem::GetFileAsString(fxl::StringView path, std::string* result) {
  if (!reader_)
    return false;
  DirectoryTableEntry entry;
  if (!reader_->GetDirectoryEntryByPath(path, &entry))
    return false;
  std::string data;
  data.resize(entry.data_length);
  size_t actual;
  zx_status_t status = zx_vmo_read(vmo_, &data[0], entry.data_offset,
                                   entry.data_length, &actual);
  if (status != ZX_OK || actual != entry.data_length)
    return false;
  result->swap(data);
  return true;
}

void FileSystem::CreateDirectory() {
  if (!reader_ || !reader_->Read())
    return;

  std::vector<DirRecord> stack;
  stack.push_back(DirRecord());
  fxl::StringView current_dir;

  reader_->ListDirectory([&](const DirectoryTableEntry& entry) {
    fxl::StringView path = reader_->GetPathView(entry);

    while (path.substr(0, current_dir.size()) != current_dir)
      LeaveDirectory(PopLastDirectory(&current_dir), &stack);

    fxl::StringView remaining = path.substr(current_dir.size());
    while (PopFirstDirectory(&remaining))
      stack.push_back(DirRecord());  // Enter directory.

    current_dir = path.substr(0, path.size() - remaining.size());

    DirRecord& parent = stack.back();
    parent.names.push_back(ToStringPiece(remaining));
    parent.children.push_back(CreateFile(vmo_, entry));
  });

  while (!current_dir.empty())
    LeaveDirectory(PopLastDirectory(&current_dir), &stack);

  FXL_DCHECK(stack.size() == 1);

  directory_ = stack.back().CreateDirectory();
}

}  // namespace archive
