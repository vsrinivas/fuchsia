// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/decompress.h>

#include <memory>
#include <new>

namespace zbitl {
namespace decompress {

fit::result<std::string_view, std::unique_ptr<std::byte[]>> DefaultAllocator(size_t bytes) {
  if (auto ptr = std::make_unique<std::byte[]>(bytes)) {
    return fit::ok(std::move(ptr));
  }
  return fit::error{"out of memory"};
}

}  // namespace decompress
}  // namespace zbitl
