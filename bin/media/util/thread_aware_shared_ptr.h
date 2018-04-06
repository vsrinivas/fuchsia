// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

namespace media {

template <typename T>
struct ThreadAwareDeleter {
  ThreadAwareDeleter(async_t* async) : async_(async) {}

  void operator()(T* t) {
    if (async_ == async_get_default()) {
      delete t;
    } else {
      async::PostTask(async_, [t]() { delete t; });
    }
  }

 private:
  async_t* async_;
};

template <typename T>
std::shared_ptr<T> ThreadAwareSharedPtr(T* t, async_t* async) {
  return std::shared_ptr<T>(t, ThreadAwareDeleter<T>(async));
}

}  // namespace media
