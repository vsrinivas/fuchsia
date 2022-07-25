// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/event_sender.h>
#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/connection.h>
#include <lib/vfs/cpp/internal/node.h>
#include <lib/vfs/cpp/internal/node_connection.h>
#include <zircon/assert.h>

#include <algorithm>
#include <mutex>

namespace vfs {
namespace {

constexpr fuchsia::io::OpenFlags kCommonAllowedFlags =
    fuchsia::io::OpenFlags::DESCRIBE | fuchsia::io::OpenFlags::NODE_REFERENCE |
    fuchsia::io::OpenFlags::POSIX_WRITABLE | fuchsia::io::OpenFlags::POSIX_EXECUTABLE |
    fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS;

constexpr std::tuple<NodeKind::Type, fuchsia::io::OpenFlags> kKindFlagMap[] = {
    {NodeKind::kReadable, fuchsia::io::OpenFlags::RIGHT_READABLE},
    {NodeKind::kWritable, fuchsia::io::OpenFlags::RIGHT_WRITABLE},
    {NodeKind::kExecutable, fuchsia::io::OpenFlags::RIGHT_EXECUTABLE},
    {NodeKind::kAppendable, fuchsia::io::OpenFlags::APPEND},
    {NodeKind::kCanTruncate, fuchsia::io::OpenFlags::TRUNCATE},
    {NodeKind::kCreatable,
     fuchsia::io::OpenFlags::CREATE | fuchsia::io::OpenFlags::CREATE_IF_ABSENT}};

}  // namespace

namespace internal {

bool IsValidName(const std::string& name) {
  return name != "." && name != ".." && name.length() <= NAME_MAX &&
         name.find('/') == std::string::npos;
}

Node::Node() = default;
Node::~Node() = default;

std::unique_ptr<Connection> Node::Close(Connection* connection) {
  std::lock_guard<std::mutex> guard(mutex_);

  auto connection_iterator =
      std::find_if(connections_.begin(), connections_.end(),
                   [connection](const auto& entry) { return entry.get() == connection; });
  auto ret = std::move(*connection_iterator);
  connections_.erase(connection_iterator);
  return ret;
}

zx_status_t Node::PreClose(Connection* connection) { return ZX_OK; }

zx_status_t Node::Sync() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Node::GetAttr(fuchsia::io::NodeAttributes* out_attributes) const {
  return ZX_ERR_NOT_SUPPORTED;
}

void Node::Clone(fuchsia::io::OpenFlags flags, fuchsia::io::OpenFlags parent_flags,
                 zx::channel request, async_dispatcher_t* dispatcher) {
  if (!Flags::InputPrecondition(flags)) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }
  // If SAME_RIGHTS is specified, the client cannot request any specific rights.
  if (Flags::ShouldCloneWithSameRights(flags) &&
      (flags & Flags::kFsRights) != fuchsia::io::OpenFlags()) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }
  flags |= (parent_flags & Flags::kStatusFlags);
  // If SAME_RIGHTS is requested, cloned connection will inherit the same rights
  // as those from the originating connection.
  if (Flags::ShouldCloneWithSameRights(flags)) {
    flags &= (~Flags::kFsRights);
    flags |= (parent_flags & Flags::kFsRights);
    flags &= ~fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS;
  }
  if (!Flags::StricterOrSameRights(flags, parent_flags)) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_ACCESS_DENIED);
    return;
  }
  Serve(flags, std::move(request), dispatcher);
}

zx_status_t Node::ValidateFlags(fuchsia::io::OpenFlags flags) const {
  if (!Flags::InputPrecondition(flags)) {
    return ZX_ERR_INVALID_ARGS;
  }
  bool is_directory = IsDirectory();
  if (!is_directory && Flags::IsDirectory(flags)) {
    return ZX_ERR_NOT_DIR;
  }
  if (is_directory && Flags::IsNotDirectory(flags)) {
    return ZX_ERR_NOT_FILE;
  }

  fuchsia::io::OpenFlags allowed_flags = kCommonAllowedFlags | GetAllowedFlags();
  if (is_directory) {
    allowed_flags = allowed_flags | fuchsia::io::OpenFlags::DIRECTORY;
  } else {
    allowed_flags = allowed_flags | fuchsia::io::OpenFlags::NOT_DIRECTORY;
  }

  fuchsia::io::OpenFlags prohibitive_flags = GetProhibitiveFlags();

  if ((flags & prohibitive_flags) != fuchsia::io::OpenFlags()) {
    return ZX_ERR_INVALID_ARGS;
  }
  if ((flags & ~allowed_flags) != fuchsia::io::OpenFlags()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Only allow executable rights on directories.
  // Changing this can be dangerous! Flags operations may have security implications.
  if (!is_directory && Flags::IsExecutable(flags)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Node::Lookup(const std::string& name, Node** out_node) const {
  ZX_ASSERT(!IsDirectory());
  return ZX_ERR_NOT_DIR;
}

fuchsia::io::OpenFlags Node::GetAllowedFlags() const {
  NodeKind::Type kind = GetKind();
  fuchsia::io::OpenFlags flags = {};
  for (auto& tuple : kKindFlagMap) {
    if ((kind & std::get<0>(tuple)) == std::get<0>(tuple)) {
      flags = flags | std::get<1>(tuple);
    }
  }
  return flags;
}

fuchsia::io::OpenFlags Node::GetProhibitiveFlags() const {
  NodeKind::Type kind = GetKind();
  if (NodeKind::IsDirectory(kind)) {
    return fuchsia::io::OpenFlags::CREATE | fuchsia::io::OpenFlags::CREATE_IF_ABSENT |
           fuchsia::io::OpenFlags::TRUNCATE | fuchsia::io::OpenFlags::APPEND;
  }
  return {};
}

zx_status_t Node::SetAttr(fuchsia::io::NodeAttributeFlags flags,
                          const fuchsia::io::NodeAttributes& attributes) {
  return ZX_ERR_NOT_SUPPORTED;
}

fuchsia::io::OpenFlags Node::FilterRefFlags(fuchsia::io::OpenFlags flags) {
  if (Flags::IsNodeReference(flags)) {
    return flags & (kCommonAllowedFlags | fuchsia::io::OpenFlags::DIRECTORY);
  }
  return flags;
}

zx_status_t Node::Serve(fuchsia::io::OpenFlags flags, zx::channel request,
                        async_dispatcher_t* dispatcher) {
  flags = FilterRefFlags(flags);
  zx_status_t status = ValidateFlags(flags);
  if (status != ZX_OK) {
    SendOnOpenEventOnError(flags, std::move(request), status);
    return status;
  }
  return Connect(flags, std::move(request), dispatcher);
}

zx_status_t Node::Connect(fuchsia::io::OpenFlags flags, zx::channel request,
                          async_dispatcher_t* dispatcher) {
  zx_status_t status;
  std::unique_ptr<Connection> connection;
  if (Flags::IsNodeReference(flags)) {
    status = Node::CreateConnection(flags, &connection);
  } else {
    status = CreateConnection(flags, &connection);
  }
  if (status != ZX_OK) {
    SendOnOpenEventOnError(flags, std::move(request), status);
    return status;
  }
  status = connection->Bind(std::move(request), dispatcher);
  if (status == ZX_OK) {
    AddConnection(std::move(connection));
  }
  return status;
}

void Node::SendOnOpenEventOnError(fuchsia::io::OpenFlags flags, zx::channel request,
                                  zx_status_t status) {
  ZX_DEBUG_ASSERT(status != ZX_OK);

  if (!Flags::ShouldDescribe(flags)) {
    return;
  }

  fidl::EventSender<fuchsia::io::Node> sender(std::move(request));
  sender.events().OnOpen(status, nullptr);
}

uint64_t Node::GetConnectionCount() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return connections_.size();
}

void Node::AddConnection(std::unique_ptr<Connection> connection) {
  std::lock_guard<std::mutex> guard(mutex_);
  connections_.push_back(std::move(connection));
}

zx_status_t Node::CreateConnection(fuchsia::io::OpenFlags flags,
                                   std::unique_ptr<Connection>* connection) {
  *connection = std::make_unique<internal::NodeConnection>(flags, this);
  return ZX_OK;
}

}  // namespace internal
}  // namespace vfs
