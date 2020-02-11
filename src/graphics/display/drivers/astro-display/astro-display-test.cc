// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "astro-display.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mock-sysmem/mock-buffer-collection.h>

#include "zxtest/zxtest.h"

namespace sysmem = llcpp::fuchsia::sysmem;

class MockBufferCollection : public mock_sysmem::MockBufferCollection {
 public:
  void SetConstraints(bool has_constraints, sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync _completer) override {
    EXPECT_TRUE(constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(constraints.buffer_memory_constraints.cpu_domain_supported);
    EXPECT_EQ(64u, constraints.image_format_constraints[0].bytes_per_row_divisor);
    set_constraints_called_ = true;
  }

  bool set_constraints_called() const { return set_constraints_called_; }

 private:
  bool set_constraints_called_ = false;
};

TEST(AstroDisplay, SysmemRequirements) {
  astro_display::AstroDisplay display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_TRUE(collection.set_constraints_called());
}
