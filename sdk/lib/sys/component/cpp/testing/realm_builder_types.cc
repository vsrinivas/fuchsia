// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/component/cpp/testing/internal/errors.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>

#include <memory>

namespace component_testing {

namespace {

constexpr char kSvcDirectoryPath[] = "/svc";

// Checks that path doesn't contain leading nor trailing slashes.
bool IsValidPath(std::string_view path) {
  return !path.empty() && path.front() != '/' && path.back() != '/';
}
}  // namespace

LocalComponent::~LocalComponent() = default;

void LocalComponent::Start(std::unique_ptr<LocalComponentHandles> mock_handles) {}

LocalComponentHandles::LocalComponentHandles(fdio_ns_t* ns, sys::OutgoingDirectory outgoing_dir)
    : namespace_(ns), outgoing_dir_(std::move(outgoing_dir)) {}

LocalComponentHandles::~LocalComponentHandles() { ZX_ASSERT(fdio_ns_destroy(namespace_) == ZX_OK); }

LocalComponentHandles::LocalComponentHandles(LocalComponentHandles&& other) noexcept
    : namespace_(other.namespace_), outgoing_dir_(std::move(other.outgoing_dir_)) {
  other.namespace_ = nullptr;
}

LocalComponentHandles& LocalComponentHandles::operator=(LocalComponentHandles&& other) noexcept {
  namespace_ = other.namespace_;
  outgoing_dir_ = std::move(other.outgoing_dir_);
  other.namespace_ = nullptr;
  return *this;
}

fdio_ns_t* LocalComponentHandles::ns() { return namespace_; }

sys::OutgoingDirectory* LocalComponentHandles::outgoing() { return &outgoing_dir_; }

sys::ServiceDirectory LocalComponentHandles::svc() {
  zx::channel local;
  zx::channel remote;
  ZX_ASSERT(zx::channel::create(0, &local, &remote) == ZX_OK);
  ZX_ASSERT(fdio_ns_connect(namespace_, kSvcDirectoryPath,
                            fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                            remote.release()) == ZX_OK);
  return sys::ServiceDirectory(std::move(local));
}

constexpr size_t kDefaultVmoSize = 4096;

DirectoryContents& DirectoryContents::AddFile(std::string_view path, BinaryContents contents) {
  ZX_ASSERT_MSG(IsValidPath(path), "[DirectoryContents/AddFile] Encountered invalid path: %s",
                path.data());

  zx::vmo vmo;
  ZX_COMPONENT_ASSERT_STATUS_OK("AddFile/zx_vmo_create", zx::vmo::create(kDefaultVmoSize, 0, &vmo));
  ZX_COMPONENT_ASSERT_STATUS_OK("AddFile/zx_vmo_write",
                                vmo.write(contents.buffer, contents.offset, contents.size));
  fuchsia::mem::Buffer out_buffer{.vmo = std::move(vmo), .size = contents.size};
  contents_.entries.emplace_back(fuchsia::component::test::DirectoryEntry{
      .file_path = std::string(path), .file_contents = std::move(out_buffer)});
  return *this;
}

DirectoryContents& DirectoryContents::AddFile(std::string_view path, std::string_view contents) {
  return AddFile(path,
                 BinaryContents{.buffer = contents.data(), .size = contents.size(), .offset = 0});
}

fuchsia::component::test::DirectoryContents DirectoryContents::TakeAsFidl() {
  return std::move(contents_);
}

}  // namespace component_testing
