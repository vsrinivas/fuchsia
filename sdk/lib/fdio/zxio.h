// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_ZXIO_H_
#define LIB_FDIO_ZXIO_H_

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/zxio/inception.h>

#include "internal.h"

namespace fdio_internal {

struct zxio : public base {
  static zx::status<fdio_ptr> create();

  zx_status_t close() override;
  zx_status_t clone(zx_handle_t* out_handle) override;
  zx_status_t unwrap(zx_handle_t* out_handle) override;
  void wait_begin(uint32_t events, zx_handle_t* out_handle, zx_signals_t* out_signals) override;
  void wait_end(zx_signals_t signals, uint32_t* out_events) override;
  zx_status_t get_token(zx_handle_t* out) override;
  zx_status_t get_attr(zxio_node_attributes_t* out) override;
  zx_status_t set_attr(const zxio_node_attributes_t* attr) override;
  zx_status_t dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory) override;
  zx_status_t dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                   zxio_dirent_t** out_entry) override;
  void dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) override;
  zx_status_t unlink(const char* name, size_t len, int flags) override;
  zx_status_t truncate(uint64_t off) override;
  zx_status_t rename(const char* src, size_t srclen, zx_handle_t dst_token, const char* dst,
                     size_t dstlen) override;
  zx_status_t link(const char* src, size_t srclen, zx_handle_t dst_token, const char* dst,
                   size_t dstlen) override;
  zx_status_t get_flags(uint32_t* out_flags) override;
  zx_status_t set_flags(uint32_t flags) override;
  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override;
  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<zxio>;
  friend class fbl::RefPtr<zxio>;

  zxio() = default;
  ~zxio() override = default;

  void wait_begin_inner(uint32_t events, zx_signals_t signals, zx_handle_t* out_handle,
                        zx_signals_t* out_signals);
  void wait_end_inner(zx_signals_t signals, uint32_t* out_events, zx_signals_t* out_signals);
  zx_status_t recvmsg_inner(struct msghdr* msg, int flags, size_t* out_actual);
  zx_status_t sendmsg_inner(const struct msghdr* msg, int flags, size_t* out_actual);
};

struct pipe : public zxio {
  static zx::status<fdio_ptr> create(zx::socket socket);
  static zx::status<std::pair<fdio_ptr, fdio_ptr>> create_pair(uint32_t options);

  Errno posix_ioctl(int request, va_list va) override;
  zx_status_t shutdown(int how, int16_t* out_code) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<pipe>;
  friend class fbl::RefPtr<pipe>;

  pipe() = default;
  ~pipe() override = default;

  static Errno posix_ioctl_inner(const zx::socket& socket, int request, va_list va);
  static zx_status_t shutdown_inner(const zx::socket& socket, int how);

 private:
  const zxio_pipe_t& zxio_pipe() { return *reinterpret_cast<zxio_pipe_t*>(&zxio_storage().io); }
};

struct remote : public zxio {
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::Node> node, zx::eventpair event);
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::File> file, zx::event event,
                                     zx::stream stream);
  static zx::status<fdio_ptr> create(zx::vmo vmo, zx::stream stream);

  // Creates an |fdio_t| for a VMO file.
  //
  // * |vmo| is the VMO that contains the contents of the file.
  // * |offset| is the index of the first byte of the file in the VMO.
  // * |length| is the number of bytes in the file.
  // * |seek| is the initial seek offset within the file (i.e., relative to
  //   |offset| within the underlying VMO).
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::File> file, zx::vmo vmo,
                                     zx_off_t offset, zx_off_t length, zx_off_t seek);

  zx::status<fdio_ptr> open(const char* path, uint32_t flags, uint32_t mode) override;
  zx_status_t borrow_channel(zx_handle_t* out_borrowed) override;
  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* signals) override;
  void wait_end(zx_signals_t signals, uint32_t* events) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<remote>;
  friend class fbl::RefPtr<remote>;

  remote() = default;
  ~remote() override = default;

  const zxio_remote_t& zxio_remote() {
    return *reinterpret_cast<zxio_remote_t*>(&zxio_storage().io);
  }
};

struct dir : public remote {
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::Directory> dir);

  // Override |convert_to_posix_mode| for directories, since directories
  // have different semantics for the "rwx" bits.
  uint32_t convert_to_posix_mode(zxio_node_protocols_t protocols,
                                 zxio_abilities_t abilities) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<dir>;
  friend class fbl::RefPtr<dir>;

  dir() = default;
  ~dir() override = default;
};

struct pty : public remote {
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_hardware_pty::Device> device,
                                     zx::eventpair event);
  Errno posix_ioctl(int request, va_list va) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<pty>;
  friend class fbl::RefPtr<pty>;

  pty() = default;
  ~pty() override = default;
};

}  // namespace fdio_internal

#endif  // LIB_FDIO_ZXIO_H_
