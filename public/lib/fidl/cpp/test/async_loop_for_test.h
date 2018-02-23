// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TEST_ASYNC_LOOP_FOR_TEST_H_
#define LIB_FIDL_CPP_TEST_ASYNC_LOOP_FOR_TEST_H_

#include <async/dispatcher.h>
#include <zircon/types.h>
#include <memory>

// Implement this class in order to run the fidl bindings unit tests on top of
// your choice of async dispatcher.
namespace fidl {
namespace test {

class AsyncLoopForTestImpl;

class AsyncLoopForTest {
 public:
  // The AsyncLoopForTest constructor should also call async_set_default() with
  // the chosen dispatcher implementation.
  AsyncLoopForTest();
  ~AsyncLoopForTest();

  // This call match the behavior of async_loop_run_until_idle().
  zx_status_t RunUntilIdle();

  // Returns the underlying async_t.
  async_t* async();

 private:
  std::unique_ptr<AsyncLoopForTestImpl> impl_;
};

}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_TEST_ASYNC_LOOP_FOR_TEST_H_
