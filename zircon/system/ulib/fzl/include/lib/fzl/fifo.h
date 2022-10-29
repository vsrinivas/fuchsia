// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FZL_FIFO_H_
#define LIB_FZL_FIFO_H_

#include <lib/zx/fifo.h>

#include <utility>

namespace fzl {

template <typename W, typename R = W>
class fifo {
  static_assert(sizeof(W) == sizeof(R), "W and R must have the same size");

 public:
  constexpr fifo() = default;

  explicit fifo(zx::fifo&& fifo) : fifo_(std::move(fifo)) {}

  explicit fifo(zx_handle_t value) : fifo_(value) {}

  explicit fifo(zx::handle&& h) : fifo_(std::move(h)) {}

  void reset(zx_handle_t value = ZX_HANDLE_INVALID) { fifo_.reset(value); }

  zx::fifo& get() { return fifo_; }
  const zx::fifo& get() const { return fifo_; }

  zx_handle_t get_handle() const { return fifo_.get(); }

  zx_handle_t release() { return fifo_.release(); }

  zx_status_t replace(zx_rights_t rights, fifo* result) {
    return fifo_.replace(rights, &result->fifo_);
  }

  zx_status_t wait_one(zx_signals_t signals, zx::time deadline, zx_signals_t* pending) const {
    return fifo_.wait_one(signals, deadline, pending);
  }

  zx_status_t signal(uint32_t clear_mask, uint32_t set_mask) const {
    return fifo_.signal(clear_mask, set_mask);
  }

  zx_status_t write(const W* buffer, size_t count, size_t* actual_count) const {
    return fifo_.write(sizeof(W), buffer, count, actual_count);
  }

  zx_status_t write_one(const W& element) const {
    return fifo_.write(sizeof(W), &element, 1, nullptr);
  }

  zx_status_t read(R* buffer, size_t count, size_t* actual_count) const {
    return fifo_.read(sizeof(R), buffer, count, actual_count);
  }

  zx_status_t read_one(R* element) const { return fifo_.read(sizeof(R), element, 1, nullptr); }

 private:
  zx::fifo fifo_;
};

template <typename W, typename R>
zx_status_t create_fifo(uint32_t elem_count, uint32_t options, fifo<W, R>* out0, fifo<R, W>* out1) {
  if (out0 == static_cast<void*>(out1)) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_handle_t h0 = ZX_HANDLE_INVALID, h1 = ZX_HANDLE_INVALID;
  zx_status_t result = zx_fifo_create(elem_count, sizeof(W), options, &h0, &h1);
  out0->reset(h0);
  out1->reset(h1);
  return result;
}

}  // namespace fzl

#endif  // LIB_FZL_FIFO_H_
