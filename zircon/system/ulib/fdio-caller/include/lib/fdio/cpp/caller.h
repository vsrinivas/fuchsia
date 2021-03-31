// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_CPP_CALLER_H_
#define LIB_FDIO_CPP_CALLER_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/zx/channel.h>

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

  FdioCaller& operator=(FdioCaller&& o) {
    fd_ = std::move(o.fd_);
    io_ = o.io_;
    o.io_ = nullptr;
    return *this;
  }
  FdioCaller(FdioCaller&& o) : fd_(std::move(o.fd_)), io_(o.io_) { o.io_ = nullptr; }
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
