// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/testing/internal/errors.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <memory>

namespace sys::testing {

namespace {

constexpr char kSvcDirectoryPath[] = "/svc";
}

MockHandles::MockHandles(fdio_ns_t* ns, OutgoingDirectory outgoing_dir)
    : namespace_(ns), outgoing_dir_(std::move(outgoing_dir)) {}

MockHandles::~MockHandles() { ZX_ASSERT(fdio_ns_destroy(namespace_) == ZX_OK); }

MockHandles::MockHandles(MockHandles&& other) noexcept
    : namespace_(other.namespace_), outgoing_dir_(std::move(other.outgoing_dir_)) {
  other.namespace_ = nullptr;
}

MockHandles& MockHandles::operator=(MockHandles&& other) noexcept {
  namespace_ = other.namespace_;
  outgoing_dir_ = std::move(other.outgoing_dir_);
  other.namespace_ = nullptr;
  return *this;
}

fdio_ns_t* MockHandles::ns() { return namespace_; }

OutgoingDirectory* MockHandles::outgoing() { return &outgoing_dir_; }

ServiceDirectory MockHandles::svc() {
  zx::channel local;
  zx::channel remote;
  ZX_ASSERT(zx::channel::create(0, &local, &remote) == ZX_OK);
  ZX_ASSERT(fdio_ns_connect(namespace_, kSvcDirectoryPath,
                            fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                            remote.release()) == ZX_OK);
  return ServiceDirectory(std::move(local));
}

}  // namespace sys::testing
