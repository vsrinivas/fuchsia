// Copyright (c) 2021 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/windowed_uint_property.h"

#include <lib/inspect/cpp/inspect.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan::brcmfmac {

constexpr size_t kMaxQueueCapacity = 128;

zx_status_t WindowedUintProperty::Init(inspect::Node* root, uint32_t window_size,
                                       const std::string& name, uint64_t value) {
  // We set an artificial limit on window_size to ensure memory usage is bounded. This dictates how
  // large the queue can get.
  if (window_size > kMaxQueueCapacity) {
    BRCMF_ERR("%s: Queue capacity %d exceed max limit of %zu", name.c_str(), queue_capacity_,
              kMaxQueueCapacity);
    return ZX_ERR_NO_RESOURCES;
  }

  std::lock_guard<std::mutex> lock(lock_);
  count_ = value;
  queue_capacity_ = window_size;
  icount_ = root->CreateUint(name, value);

  return ZX_OK;
}

void WindowedUintProperty::Add(uint64_t value) {
  std::lock_guard<std::mutex> lock(lock_);
  count_ += value;
  icount_.Add(value);
}

void WindowedUintProperty::SlideWindow() {
  std::lock_guard<std::mutex> lock(lock_);
  count_queue_.push(count_);

  // Pop if queue exceeds its capacity (i.e. the window_size).
  if (count_queue_.size() > queue_capacity_) {
    icount_.Set(count_ - count_queue_.front());
    count_queue_.pop();
  }
}

}  // namespace wlan::brcmfmac
