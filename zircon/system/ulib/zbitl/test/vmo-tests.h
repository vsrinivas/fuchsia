// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_VMO_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_VMO_TESTS_H_

#include <lib/zbitl/vmo.h>

#include <memory>

#include "tests.h"

struct VmoTestTraits {
  using storage_type = zx::vmo;
  using payload_type = uint64_t;
  using creation_traits = VmoTestTraits;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    storage_type storage_;
  };

  static void Create(size_t size, Context* context) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(size, 0u, &vmo));
    *context = {std::move(vmo)};
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    ASSERT_TRUE(fd);
    std::unique_ptr<std::byte[]> buff{new std::byte[size]};
    ASSERT_EQ(size, read(fd.get(), buff.get(), size), "%s", strerror(errno));
    ASSERT_NO_FATAL_FAILURES(Create(size, context));
    ASSERT_OK(context->storage_.write(buff.get(), 0u, size));
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    contents->resize(size);
    ASSERT_EQ(ZX_OK, storage.read(contents->data(), payload, size));
  }

  static payload_type AsPayload(const storage_type& storage) { return 0; }

  static const zx::vmo& GetVmo(const storage_type& storage) { return storage; }
};

struct UnownedVmoTestTraits {
  using storage_type = zx::unowned_vmo;
  using payload_type = uint64_t;
  using creation_traits = VmoTestTraits;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    storage_type storage_;
    zx::vmo keepalive_;
  };

  static void Create(size_t size, Context* context) {
    typename VmoTestTraits::Context vmo_context;
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Create(size, &vmo_context));
    context->storage_ = zx::unowned_vmo{vmo_context.storage_};
    context->keepalive_ = std::move(vmo_context.storage_);
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    typename VmoTestTraits::Context vmo_context;
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Create(std::move(fd), size, &vmo_context));
    context->storage_ = zx::unowned_vmo{vmo_context.storage_};
    context->keepalive_ = std::move(vmo_context.storage_);
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Read(*storage, payload, size, contents));
  }

  static payload_type AsPayload(const storage_type& storage) { return 0; }

  static const zx::vmo& GetVmo(const storage_type& storage) { return *storage; }
};

struct MapOwnedVmoTestTraits {
  using storage_type = zbitl::MapOwnedVmo;
  using payload_type = uint64_t;
  using creation_traits = MapOwnedVmoTestTraits;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    storage_type storage_;
  };

  static void Create(size_t size, Context* context) {
    typename VmoTestTraits::Context vmo_context;
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Create(size, &vmo_context));
    *context = {zbitl::MapOwnedVmo{std::move(vmo_context.storage_)}};
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    typename VmoTestTraits::Context vmo_context;
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Create(std::move(fd), size, &vmo_context));
    *context = {zbitl::MapOwnedVmo{vmo_context.TakeStorage()}};
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Read(storage.vmo(), payload, size, contents));
  }

  static payload_type AsPayload(const storage_type& storage) { return 0; }

  static const zx::vmo& GetVmo(const storage_type& storage) { return storage.vmo(); }
};

struct MapUnownedVmoTestTraits {
  using storage_type = zbitl::MapUnownedVmo;
  using payload_type = uint64_t;
  using creation_traits = MapOwnedVmoTestTraits;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;

  struct Context {
    storage_type TakeStorage() { return std::move(storage_); }

    storage_type storage_;
    zx::vmo keepalive_;
  };

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    typename UnownedVmoTestTraits::Context unowned_vmo_context;
    ASSERT_NO_FATAL_FAILURES(
        UnownedVmoTestTraits::Create(std::move(fd), size, &unowned_vmo_context));
    context->storage_ = zbitl::MapUnownedVmo{std::move(unowned_vmo_context.storage_)};
    context->keepalive_ = std::move(unowned_vmo_context.keepalive_);
  }

  static void Create(size_t size, Context* context) {
    typename UnownedVmoTestTraits::Context unowned_vmo_context;
    ASSERT_NO_FATAL_FAILURES(UnownedVmoTestTraits::Create(size, &unowned_vmo_context));
    *context = {zbitl::MapUnownedVmo{std::move(unowned_vmo_context.storage_)},
                std::move(unowned_vmo_context.keepalive_)};
  }

  static void Read(const storage_type& storage, payload_type payload, size_t size,
                   Bytes* contents) {
    ASSERT_NO_FATAL_FAILURES(VmoTestTraits::Read(storage.vmo(), payload, size, contents));
  }

  static payload_type AsPayload(const storage_type& storage) { return 0; }

  static const zx::vmo& GetVmo(const storage_type& storage) { return storage.vmo(); }
};

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_VMO_TESTS_H_
