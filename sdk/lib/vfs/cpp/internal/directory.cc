
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/directory.h>
#include <lib/vfs/cpp/internal/directory_connection.h>
#include <zircon/errors.h>

#include <string_view>

namespace vfs {
namespace {

/// Validates consistency of mode and flags passed to an Open call.
///
/// As the SDK VFS does not support the CREATE flags, as per the fuchsia.io protocol, the
/// mode only needs to be consistent with the flags/path if the resource already exists
/// (i.e. even if the path points to a file that reports it's mode as MODE_TYPE_FILE, it is
/// not an error to open the file with MODE_TYPE_DIRECTORY).
bool ValidateMode(uint32_t mode, fuchsia::io::OpenFlags flags, std::string_view path) {
  if (path.empty()) {
    return false;
  }

  const uint32_t mode_type = mode & fuchsia::io::MODE_TYPE_MASK;

  // The mode type must always be consistent with OPEN_FLAG_DIRECTORY/NOT_DIRECTORY.
  if ((mode_type == fuchsia::io::MODE_TYPE_DIRECTORY) && Flags::IsNotDirectory(flags)) {
    return false;
  }
  if ((mode_type == fuchsia::io::MODE_TYPE_FILE) && Flags::IsDirectory(flags)) {
    return false;
  }

  // Can't open "." with OPEN_FLAG_NOT_DIRECTORY
  if (Flags::IsNotDirectory(flags) && (path == ".")) {
    return false;
  }
  // If the path specifies a directory (indicated by a trailing slash), reject
  // either OPEN_FLAG_NOT_DIRECTORY or MODE_TYPE_FILE.
  if (path.back() == '/') {
    return !(Flags::IsNotDirectory(flags) || (mode_type == fuchsia::io::MODE_TYPE_FILE));
  }

  return true;
}

}  // namespace

namespace internal {

Directory::Directory() = default;

Directory::~Directory() = default;

void Directory::Describe(fuchsia::io::NodeInfoDeprecated* out_info) {
  out_info->set_directory(fuchsia::io::DirectoryObject());
}

void Directory::GetConnectionInfo(fuchsia::io::ConnectionInfo* out_info) { *out_info = {}; }

zx_status_t Directory::Lookup(const std::string& name, Node** out_node) const {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Directory::CreateConnection(fuchsia::io::OpenFlags flags,
                                        std::unique_ptr<Connection>* connection) {
  *connection = std::make_unique<internal::DirectoryConnection>(flags, this);
  return ZX_OK;
}

zx_status_t Directory::ValidatePath(const char* path, size_t path_len) {
  bool starts_with_dot_dot = (path_len > 1 && path[0] == '.' && path[1] == '.');
  if (path_len > NAME_MAX || (path_len == 2 && starts_with_dot_dot) ||
      (path_len > 2 && starts_with_dot_dot && path[2] == '/') || (path_len > 0 && path[0] == '/')) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

bool Directory::IsDirectory() const { return true; }

fuchsia::io::OpenFlags Directory::GetAllowedFlags() const {
  return fuchsia::io::OpenFlags::DIRECTORY | fuchsia::io::OpenFlags::RIGHT_READABLE |
         fuchsia::io::OpenFlags::RIGHT_WRITABLE | fuchsia::io::OpenFlags::RIGHT_EXECUTABLE;
}

fuchsia::io::OpenFlags Directory::GetProhibitiveFlags() const {
  return fuchsia::io::OpenFlags::CREATE | fuchsia::io::OpenFlags::CREATE_IF_ABSENT |
         fuchsia::io::OpenFlags::TRUNCATE | fuchsia::io::OpenFlags::APPEND;
}

zx_status_t Directory::GetAttr(fuchsia::io::NodeAttributes* out_attributes) const {
  out_attributes->mode = fuchsia::io::MODE_TYPE_DIRECTORY | V_IRUSR;
  out_attributes->id = fuchsia::io::INO_UNKNOWN;
  out_attributes->content_size = 0;
  out_attributes->storage_size = 0;
  out_attributes->link_count = 1;
  out_attributes->creation_time = 0;
  out_attributes->modification_time = 0;
  return ZX_OK;
}

zx_status_t Directory::WalkPath(const char* path, size_t path_len, const char** out_path,
                                size_t* out_len, std::string* out_key, bool* out_is_self) {
  *out_path = path;
  *out_len = path_len;
  *out_is_self = false;
  zx_status_t status = ValidatePath(path, path_len);
  if (status != ZX_OK) {
    return status;
  }

  // remove any "./", ".//", etc
  while (path_len > 1 && path[0] == '.' && path[1] == '/') {
    path += 2;
    path_len -= 2;
    size_t index = 0u;
    while (index < path_len && path[index] == '/') {
      index++;
    }
    path += index;
    path_len -= index;
  }

  *out_path = path;
  *out_len = path_len;

  if (path_len == 0 || (path_len == 1 && path[0] == '.')) {
    *out_is_self = true;
    return ZX_OK;
  }

  // Lookup node
  const char* path_end = path + path_len;
  const char* match = std::find(path, path_end, '/');

  if (path_end == match) {
    // "/" not found
    *out_key = std::string(path, path_len);
    *out_len = 0;
    *out_path = path_end;
  } else {
    size_t index = std::distance(path, match);
    *out_key = std::string(path, index);

    // remove all '/'
    while (index < path_len && path[index] == '/') {
      index++;
    }
    *out_len -= index;
    *out_path += index;
  }
  return ZX_OK;
}

zx_status_t Directory::LookupPath(const char* path, size_t path_len, bool* out_is_dir,
                                  Node** out_node, const char** out_path, size_t* out_len) {
  Node* current_node = this;
  size_t new_path_len = path_len;
  const char* new_path = path;
  *out_is_dir = path_len == 0 || path[path_len - 1] == '/';
  do {
    std::string key;
    bool is_self = false;
    zx_status_t status = WalkPath(new_path, new_path_len, &new_path, &new_path_len, &key, &is_self);
    if (status != ZX_OK) {
      return status;
    }
    if (is_self) {
      *out_is_dir = true;
      *out_node = current_node;
      return ZX_OK;
    }
    Node* n = nullptr;
    status = current_node->Lookup(key, &n);
    if (status != ZX_OK) {
      return status;
    }
    current_node = n;
    if (current_node->IsRemote()) {
      break;
    }
  } while (new_path_len > 0);

  *out_node = current_node;
  *out_len = new_path_len;
  *out_path = new_path;
  return ZX_OK;
}

void Directory::Open(fuchsia::io::OpenFlags open_flags, fuchsia::io::OpenFlags parent_flags,
                     uint32_t mode, const char* path, size_t path_len, zx::channel request,
                     async_dispatcher_t* dispatcher) {
  if (!Flags::InputPrecondition(open_flags)) {
    Node::SendOnOpenEventOnError(open_flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }
  if (Flags::ShouldCloneWithSameRights(open_flags)) {
    Node::SendOnOpenEventOnError(open_flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }
  if (!Flags::StricterOrSameRights(open_flags, parent_flags)) {
    Node::SendOnOpenEventOnError(open_flags, std::move(request), ZX_ERR_ACCESS_DENIED);
    return;
  }
  if ((path_len < 1) || (path_len > PATH_MAX)) {
    Node::SendOnOpenEventOnError(open_flags, std::move(request), ZX_ERR_BAD_PATH);
    return;
  }
  if (!ValidateMode(mode, open_flags, std::string_view{path, path_len})) {
    Node::SendOnOpenEventOnError(open_flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }

  Node* n = nullptr;
  bool path_is_dir = false;
  size_t new_path_len = path_len;
  const char* new_path = path;
  /// TODO(fxbug.dev/82672): Path handling does not conform to in-tree behavior. Update to comply
  /// with open_path() test in io1_tests.rs, and remove `non_conformant_path_handling` option.
  zx_status_t status = LookupPath(path, path_len, &path_is_dir, &n, &new_path, &new_path_len);
  if (status != ZX_OK) {
    return SendOnOpenEventOnError(open_flags, std::move(request), status);
  }

  // The POSIX compatibility flags allow the child dir connection to inherit
  // write/execute rights from its immediate parent. We remove the right inheritance
  // anywhere the parent node does not have the relevant right.
  if (Flags::IsPosixWritable(open_flags) && !Flags::IsWritable(parent_flags)) {
    open_flags &= ~fuchsia::io::OpenFlags::POSIX_WRITABLE;
  }
  if (Flags::IsPosixExecutable(open_flags) && !Flags::IsExecutable(parent_flags)) {
    open_flags &= ~fuchsia::io::OpenFlags::POSIX_EXECUTABLE;
  }

  if (n->IsRemote() && new_path_len > 0) {
    n->OpenRemote(open_flags, mode, std::string_view(new_path, new_path_len),
                  fidl::InterfaceRequest<fuchsia::io::Node>(std::move(request)));
    return;
  }

  // Perform POSIX rights expansion if required.
  if (n->IsDirectory()) {
    if (Flags::IsPosixWritable(open_flags))
      open_flags |= (parent_flags & fuchsia::io::OpenFlags::RIGHT_WRITABLE);
    if (Flags::IsPosixExecutable(open_flags))
      open_flags |= (parent_flags & fuchsia::io::OpenFlags::RIGHT_EXECUTABLE);
    // Strip flags now that rights have been expanded.
    open_flags &=
        ~(fuchsia::io::OpenFlags::POSIX_WRITABLE | fuchsia::io::OpenFlags::POSIX_EXECUTABLE);
  }

  if (path_is_dir) {
    open_flags |= fuchsia::io::OpenFlags::DIRECTORY;
  }
  n->Serve(open_flags, std::move(request), dispatcher);
}

}  // namespace internal
}  // namespace vfs
