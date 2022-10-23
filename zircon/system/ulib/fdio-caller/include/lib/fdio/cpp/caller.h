// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_CPP_CALLER_H_
#define LIB_FDIO_CPP_CALLER_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>

#include <utility>

#include <fbl/unique_fd.h>

namespace fdio_cpp {

// Helper utility which borrows a file descriptor to allow the caller
// to make access to channel-based calls.
//
// FdioCaller consumes |fd|, but the same |fd| may be re-acquired by
// calling "release()" on the FdioCaller object.
//
// This class is movable, but not copyable.
class FdioCaller {
 public:
  FdioCaller() : io_(nullptr) {}

  explicit FdioCaller(fbl::unique_fd fd)
      : fd_(std::move(fd)), io_(fdio_unsafe_fd_to_io(fd_.get())) {}

  FdioCaller& operator=(FdioCaller&& o) noexcept {
    fd_ = std::move(o.fd_);
    io_ = o.io_;
    o.io_ = nullptr;
    return *this;
  }
  FdioCaller(FdioCaller&& o) noexcept : fd_(std::move(o.fd_)), io_(o.io_) { o.io_ = nullptr; }
  FdioCaller(const FdioCaller&) = delete;
  FdioCaller& operator=(const FdioCaller&) = delete;

  ~FdioCaller() { release(); }

  void reset(fbl::unique_fd fd = fbl::unique_fd()) {
    release();
    fd_ = std::move(fd);
    io_ = fd_ ? fdio_unsafe_fd_to_io(fd_.get()) : nullptr;
  }

  fbl::unique_fd release() {
    if (io_ != nullptr) {
      fdio_unsafe_release(io_);
      io_ = nullptr;
    }
    return std::move(fd_);
  }

  explicit operator bool() const { return io_ != nullptr; }

  // Returns a const reference to the underlying fd.
  //
  // The reference to |fd| must not outlast the lifetime of the FdioCaller.
  const fbl::unique_fd& fd() const { return fd_; }

  // This channel is borrowed, but returned as a zx_handle_t for convenience.
  //
  // It should not be closed.
  // It should not be transferred.
  // It should not be kept alive longer than the FdioCaller object, nor should
  // it be kept alive after FdioCaller.release() is called.
  zx_handle_t borrow_channel() const { return fdio_unsafe_borrow_channel(io_); }

  // Same as borrow_channel, but wrapped using libzx wrapper to signal
  // ownership.
  zx::unowned_channel channel() const { return zx::unowned_channel(borrow_channel()); }

  // This channel is cloned.
  // The returned channel can outlive the FdioCaller object.
  zx::result<zx::channel> clone_channel() const {
    zx_handle_t handle;
    auto status = fdio_fd_clone(fd_.get(), &handle);
    if (status != ZX_OK) {
      return zx::error_result(status);
    }
    return zx::ok(zx::channel(handle));
  }

  // This channel is taken.
  // After this call this FdioCaller object and the channel that was passed in are invalid.
  zx::result<zx::channel> take_channel() {
    int fd = release().release();
    zx_handle_t handle;
    auto status = fdio_fd_transfer(fd, &handle);
    if (status != ZX_OK) {
      return zx::error_result(status);
    }
    return zx::ok(zx::channel(handle));
  }

  // Same as borrow_channel, but wrapped as a fuchsia.io/Node client channel.
  fidl::UnownedClientEnd<fuchsia_io::Node> node() const { return borrow_as<fuchsia_io::Node>(); }

  // Same as borrow_channel, but wrapped as a fuchsia.io/File client channel.
  fidl::UnownedClientEnd<fuchsia_io::File> file() const { return borrow_as<fuchsia_io::File>(); }

  // Same as borrow_channel, but wrapped as a fuchsia.io/Directory client channel.
  fidl::UnownedClientEnd<fuchsia_io::Directory> directory() const {
    return borrow_as<fuchsia_io::Directory>();
  }

  // Same as clone_channel, but wrapped as a fuchsia.io/Node client channel.
  zx::result<fidl::ClientEnd<fuchsia_io::Node>> clone_node() const {
    return clone_as<fuchsia_io::Node>();
  }

  // Same as clone_channel, but wrapped as a fuchsia.io/File client channel.
  zx::result<fidl::ClientEnd<fuchsia_io::File>> clone_file() const {
    return clone_as<fuchsia_io::File>();
  }

  // Same as clone_channel, but wrapped as a fuchsia.io/Directory client channel.
  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> clone_directory() const {
    return clone_as<fuchsia_io::Directory>();
  }

  // Same as take_channel, but wrapped as a fuchsia.io/Node client channel.
  zx::result<fidl::ClientEnd<fuchsia_io::Node>> take_node() { return take_as<fuchsia_io::Node>(); }

  // Same as take_channel, but wrapped as a fuchsia.io/File client channel.
  zx::result<fidl::ClientEnd<fuchsia_io::File>> take_file() { return take_as<fuchsia_io::File>(); }

  // Same as take_channel, but wrapped as a fuchsia.io/Directory client channel.
  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> take_directory() {
    return take_as<fuchsia_io::Directory>();
  }

  // Same as borrow_channel but wrapped in a typed client channel.
  // Be careful to only use this if you know the type of the protocol being spoken.
  template <typename T>
  fidl::UnownedClientEnd<T> borrow_as() const {
    return fidl::UnownedClientEnd<T>(channel());
  }

  // Same as clone_channel but wrapped in a typed client channel.
  // Be careful to only use this if you know the type of the protocol being spoken.
  template <typename T>
  zx::result<fidl::ClientEnd<T>> clone_as() const {
    auto channel = clone_channel();
    if (channel.is_error()) {
      return channel.take_error();
    }
    return zx::ok(fidl::ClientEnd<T>(std::move(*channel)));
  }

  // Same as take_channel but wrapped in a typed client channel.
  // Be careful to only use this if you know the type of the protocol being spoken.
  template <typename T>
  zx::result<fidl::ClientEnd<T>> take_as() {
    auto channel = take_channel();
    if (channel.is_error()) {
      return channel.take_error();
    }
    return zx::ok(fidl::ClientEnd<T>(std::move(*channel)));
  }

 private:
  fbl::unique_fd fd_;
  fdio_t* io_;
};

// Helper utility which allows a client to access an fd's underlying channel.
//
// Does not take ownership of the fd, but prevents the fdio_t object
// from being unbound from the fd.
class UnownedFdioCaller {
 public:
  UnownedFdioCaller() : io_(nullptr) {}

  explicit UnownedFdioCaller(int fd) : io_(fdio_unsafe_fd_to_io(fd)) {}
  explicit UnownedFdioCaller(const fbl::unique_fd& fd) : UnownedFdioCaller(fd.get()) {}

  ~UnownedFdioCaller() { release(); }

  void reset(int fd = -1) {
    release();
    io_ = fd >= 0 ? fdio_unsafe_fd_to_io(fd) : nullptr;
  }

  explicit operator bool() const { return io_ != nullptr; }

  // This channel is borrowed, but returned as a zx_handle_t for convenience.
  //
  // It should not be closed.
  // It should not be transferred.
  // It should not be kept alive longer than the UnownedFdioCaller object, nor should
  // it be kept alive after UnownedFdioCaller.reset() is called.
  zx_handle_t borrow_channel() const { return fdio_unsafe_borrow_channel(io_); }

  // Same as borrow_channel, but wrapped using libzx wrapper to signal
  // ownership.
  zx::unowned_channel channel() const { return zx::unowned_channel(borrow_channel()); }

  // Same as borrow_channel, but wrapped as a fuchsia.io/Node client channel.
  fidl::UnownedClientEnd<fuchsia_io::Node> node() const { return borrow_as<fuchsia_io::Node>(); }

  // Same as borrow_channel, but wrapped as a fuchsia.io/File client channel.
  fidl::UnownedClientEnd<fuchsia_io::File> file() const { return borrow_as<fuchsia_io::File>(); }

  // Same as borrow_channel, but wrapped as a fuchsia.io/Directory client channel.
  fidl::UnownedClientEnd<fuchsia_io::Directory> directory() const {
    return borrow_as<fuchsia_io::Directory>();
  }

  // Same as borrow_channel but wrapped in a typed client channel.
  // Be careful to only use this if you know the type of the protocol being spoken.
  template <typename T>
  fidl::UnownedClientEnd<T> borrow_as() const {
    return fidl::UnownedClientEnd<T>(channel());
  }

  UnownedFdioCaller& operator=(UnownedFdioCaller&& o) = delete;
  UnownedFdioCaller(UnownedFdioCaller&& o) = delete;
  UnownedFdioCaller(const UnownedFdioCaller&) = delete;
  UnownedFdioCaller& operator=(const UnownedFdioCaller&) = delete;

 private:
  void release() {
    if (io_ != nullptr) {
      fdio_unsafe_release(io_);
      io_ = nullptr;
    }
  }

  fdio_t* io_;
};

}  // namespace fdio_cpp

#endif  // LIB_FDIO_CPP_CALLER_H_
