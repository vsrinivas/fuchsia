// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/internal/file.h>
#include <lib/vfs/cpp/internal/file_connection.h>

namespace vfs {
namespace internal {

File::File() = default;
File::~File() = default;

void File::Describe(fuchsia::io::NodeInfo* out_info) {
  out_info->set_file(fuchsia::io::FileObject());
}

zx_status_t File::ReadAt(uint64_t count, uint64_t offset,
                         std::vector<uint8_t>* out_data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t File::WriteAt(std::vector<uint8_t> data, uint64_t offset,
                          uint64_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t File::Truncate(uint64_t length) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t File::CreateConnection(uint32_t flags,
                                   std::unique_ptr<Connection>* connection) {
  *connection = std::make_unique<internal::FileConnection>(flags, this);
  return ZX_OK;
}

size_t File::GetCapacity() { return std::numeric_limits<size_t>::max(); }

NodeKind::Type File::GetKind() const { return NodeKind::kFile; }

}  // namespace internal
}  // namespace vfs
