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

constexpr uint32_t kCommonAllowedFlags =
    fuchsia::io::OPEN_FLAG_DESCRIBE | fuchsia::io::OPEN_FLAG_NODE_REFERENCE |
    fuchsia::io::OPEN_FLAG_POSIX | fuchsia::io::CLONE_FLAG_SAME_RIGHTS;

constexpr std::tuple<NodeKind::Type, uint32_t> kKindFlagMap[] = {
    {NodeKind::kReadable, fuchsia::io::OPEN_RIGHT_READABLE},
    {NodeKind::kMountable, fuchsia::io::OPEN_RIGHT_ADMIN},
    {NodeKind::kWritable, fuchsia::io::OPEN_RIGHT_WRITABLE},
    {NodeKind::kAppendable, fuchsia::io::OPEN_FLAG_APPEND},
    {NodeKind::kCanTruncate, fuchsia::io::OPEN_FLAG_TRUNCATE},
    {NodeKind::kCreatable,
     fuchsia::io::OPEN_FLAG_CREATE | fuchsia::io::OPEN_FLAG_CREATE_IF_ABSENT}};

}  // namespace

namespace internal {

bool IsValidName(const std::string& name) {
  return name.length() <= NAME_MAX && memchr(name.data(), '/', name.length()) == nullptr &&
         name != "." && name != "..";
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

void Node::Clone(uint32_t flags, uint32_t parent_flags, zx::channel request,
                 async_dispatcher_t* dispatcher) {
  if (!Flags::InputPrecondition(flags)) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }
  // If SAME_RIGHTS is specified, the client cannot request any specific rights.
  if (Flags::ShouldCloneWithSameRights(flags) && (flags & Flags::kFsRights)) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_INVALID_ARGS);
    return;
  }
  flags |= (parent_flags & Flags::kStatusFlags);
  // If SAME_RIGHTS is requested, cloned connection will inherit the same rights
  // as those from the originating connection.
  if (Flags::ShouldCloneWithSameRights(flags)) {
    flags &= (~Flags::kFsRights);
    flags |= (parent_flags & Flags::kFsRights);
    flags &= ~fuchsia::io::CLONE_FLAG_SAME_RIGHTS;
  }
  if (!Flags::StricterOrSameRights(flags, parent_flags)) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_ACCESS_DENIED);
    return;
  }
  Serve(flags, std::move(request), dispatcher);
}

zx_status_t Node::ValidateFlags(uint32_t flags) const {
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

  uint32_t allowed_flags = kCommonAllowedFlags | GetAllowedFlags();
  if (is_directory) {
    allowed_flags = allowed_flags | fuchsia::io::OPEN_FLAG_DIRECTORY;
  } else {
    allowed_flags = allowed_flags | fuchsia::io::OPEN_FLAG_NOT_DIRECTORY;
  }

  uint32_t prohibitive_flags = GetProhibitiveFlags();

  if ((flags & prohibitive_flags) != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if ((flags & ~allowed_flags) != 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Node::ValidateMode(uint32_t mode) const {
  fuchsia::io::NodeAttributes attr;
  uint32_t mode_from_attr = 0;
  zx_status_t status = GetAttr(&attr);
  if (status == ZX_OK) {
    mode_from_attr = attr.mode & fuchsia::io::MODE_TYPE_MASK;
  }

  if (((mode & ~fuchsia::io::MODE_PROTECTION_MASK) & ~mode_from_attr) != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t Node::Lookup(const std::string& name, Node** out_node) const {
  ZX_ASSERT(!IsDirectory());
  return ZX_ERR_NOT_DIR;
}

uint32_t Node::GetAllowedFlags() const {
  NodeKind::Type kind = GetKind();
  uint32_t flags = 0;
  for (auto& tuple : kKindFlagMap) {
    if ((kind & std::get<0>(tuple)) == std::get<0>(tuple)) {
      flags = flags | std::get<1>(tuple);
    }
  }
  return flags;
}

uint32_t Node::GetProhibitiveFlags() const {
  NodeKind::Type kind = GetKind();
  if (NodeKind::IsDirectory(kind)) {
    return fuchsia::io::OPEN_FLAG_CREATE | fuchsia::io::OPEN_FLAG_CREATE_IF_ABSENT |
           fuchsia::io::OPEN_FLAG_TRUNCATE | fuchsia::io::OPEN_FLAG_APPEND;
  }
  return 0;
}

zx_status_t Node::SetAttr(uint32_t flags, const fuchsia::io::NodeAttributes& attributes) {
  return ZX_ERR_NOT_SUPPORTED;
}

uint32_t Node::FilterRefFlags(uint32_t flags) {
  if (Flags::IsNodeReference(flags)) {
    return flags & (kCommonAllowedFlags | fuchsia::io::OPEN_FLAG_DIRECTORY);
  }
  return flags;
}

zx_status_t Node::Serve(uint32_t flags, zx::channel request, async_dispatcher_t* dispatcher) {
  flags = FilterRefFlags(flags);
  zx_status_t status = ValidateFlags(flags);
  if (status != ZX_OK) {
    SendOnOpenEventOnError(flags, std::move(request), status);
    return status;
  }
  return Connect(flags, std::move(request), dispatcher);
}

zx_status_t Node::Connect(uint32_t flags, zx::channel request, async_dispatcher_t* dispatcher) {
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

zx_status_t Node::ServeWithMode(uint32_t flags, uint32_t mode, zx::channel request,
                                async_dispatcher_t* dispatcher) {
  zx_status_t status = ValidateMode(mode);
  if (status != ZX_OK) {
    SendOnOpenEventOnError(flags, std::move(request), status);
    return status;
  }
  return Serve(flags, std::move(request), dispatcher);
}

void Node::SendOnOpenEventOnError(uint32_t flags, zx::channel request, zx_status_t status) {
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

zx_status_t Node::CreateConnection(uint32_t flags, std::unique_ptr<Connection>* connection) {
  *connection = std::make_unique<internal::NodeConnection>(flags, this);
  return ZX_OK;
}

}  // namespace internal
}  // namespace vfs
