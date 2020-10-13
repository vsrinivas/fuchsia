// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
// CODELAB: Include the inspect test library.

#include "reverser.h"

class ReverserTest : public gtest::RealLoopFixture {
 public:
  ReverserTest() : executor_(dispatcher()) {}

 protected:
  // Creates a Reverser and return a client Ptr for it.
  fuchsia::examples::inspect::ReverserPtr OpenReverser() {
    fuchsia::examples::inspect::ReverserPtr ptr;

    // [START open_reverser]
    binding_set_.AddBinding(std::make_unique<Reverser>(ReverserStats::CreateDefault()),
                            ptr.NewRequest());
    // [END open_reverser]

    return ptr;
  }

  // Run a promise to completion on the default async executor.
  void RunPromiseToCompletion(fit::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntil([&] { return done; });
  }

  // Get the number of active connections.
  //
  // This allows us to wait until a connection closes.
  size_t connection_count() const { return binding_set_.size(); }

 private:
  async::Executor executor_;
  fidl::BindingSet<fuchsia::examples::inspect::Reverser, std::unique_ptr<Reverser>> binding_set_;
};

TEST_F(ReverserTest, ReversePart3) {
  auto ptr = OpenReverser();

  bool done = false;
  std::string value;
  ptr->Reverse("hello", [&](std::string response) {
    value = std::move(response);
    done = true;
  });
  RunLoopUntil([&] { return done; });
  EXPECT_EQ("olleh", value);
}
