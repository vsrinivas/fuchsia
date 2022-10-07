// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_ZXIO_H_
#define LIB_FDIO_ZXIO_H_

#include <lib/zxio/cpp/inception.h>

#include "sdk/lib/fdio/internal.h"

namespace fdio_internal {

struct zxio : public fdio_t {
  static zx::status<fdio_ptr> create();
  static zx::status<fdio_ptr> create_null();

  zx_status_t close() override;
  zx_status_t borrow_channel(zx_handle_t* out_borrowed) final;
  zx_status_t clone(zx_handle_t* out_handle) override;
  zx_status_t unwrap(zx_handle_t* out_handle) override;
  void wait_begin(uint32_t events, zx_handle_t* out_handle, zx_signals_t* out_signals) override;
  void wait_end(zx_signals_t signals, uint32_t* out_events) override;
  Errno posix_ioctl(int request, va_list va) override;
  zx_status_t get_token(zx_handle_t* out) override;
  zx_status_t get_attr(zxio_node_attributes_t* out) override;
  zx_status_t set_attr(const zxio_node_attributes_t* attr) override;
  zx_status_t dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory) override;
  zx_status_t dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                   zxio_dirent_t* inout_entry) override;
  void dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) override;
  zx_status_t watch_directory(zxio_watch_directory_cb cb, zx_time_t deadline,
                              void* context) override;
  zx_status_t unlink(std::string_view name, int flags) override;
  zx_status_t truncate(uint64_t off) override;
  zx_status_t rename(std::string_view src, zx_handle_t dst_token, std::string_view dst) override;
  zx_status_t link(std::string_view src, zx_handle_t dst_token, std::string_view dst) override;
  zx_status_t get_flags(fuchsia_io::wire::OpenFlags* out_flags) override;
  zx_status_t set_flags(fuchsia_io::wire::OpenFlags flags) override;
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
};

struct pipe : public zxio {
  static zx::status<fdio_ptr> create(zx::socket socket);
  static zx::status<std::pair<fdio_ptr, fdio_ptr>> create_pair(uint32_t options);

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<pipe>;
  friend class fbl::RefPtr<pipe>;

  pipe() = default;
  ~pipe() override = default;
};

struct remote : public zxio {
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::Node> node);
  static zx::status<fdio_ptr> create(zx::vmo vmo, zx::stream stream);

  zx::status<fdio_ptr> open(std::string_view path, fuchsia_io::wire::OpenFlags flags,
                            uint32_t mode) override;
  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* signals) override;
  void wait_end(zx_signals_t signals, uint32_t* events) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<remote>;
  friend class fbl::RefPtr<remote>;

  remote() = default;
  ~remote() override = default;
};

zx::status<fdio_ptr> open_async(zxio_t* directory, std::string_view path,
                                fuchsia_io::wire::OpenFlags flags, uint32_t mode);

}  // namespace fdio_internal

#endif  // LIB_FDIO_ZXIO_H_
