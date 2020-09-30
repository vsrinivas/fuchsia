// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "amlogic-display.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mock-sysmem/mock-buffer-collection.h>

#include "osd.h"
#include "zxtest/zxtest.h"

namespace sysmem = llcpp::fuchsia::sysmem;

class MockBufferCollection : public mock_sysmem::MockBufferCollection {
 public:
  void SetConstraints(bool has_constraints, sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(constraints.buffer_memory_constraints.cpu_domain_supported);
    EXPECT_EQ(64u, constraints.image_format_constraints[0].bytes_per_row_divisor);
    EXPECT_EQ(4u, constraints.image_format_constraints_count);
    EXPECT_EQ(sysmem::FORMAT_MODIFIER_ARM_LINEAR_TE,
              constraints.image_format_constraints[1].pixel_format.format_modifier.value);
    set_constraints_called_ = true;
  }

  void SetName(uint32_t priority, fidl::StringView name,
               SetNameCompleter::Sync& completer) override {
    EXPECT_EQ(10u, priority);
    EXPECT_EQ(std::string("Display"), std::string(name.data(), name.size()));
    set_name_called_ = true;
  }

  bool set_constraints_called() const { return set_constraints_called_; }
  bool set_name_called() const { return set_name_called_; }

 private:
  bool set_constraints_called_ = false;
  bool set_name_called_ = false;
};

TEST(AmlogicDisplay, SysmemRequirements) {
  amlogic_display::AmlogicDisplay display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_TRUE(collection.set_constraints_called());
  EXPECT_TRUE(collection.set_name_called());
}

TEST(AmlogicDisplay, FloatToFix3_10) {
  inspect::Inspector inspector;
  amlogic_display::Osd osd = amlogic_display::Osd(100, 100, 100, 100, &inspector.GetRoot());
  EXPECT_EQ(0x0000, osd.FloatToFixed3_10(0.0f));
  EXPECT_EQ(0x0066, osd.FloatToFixed3_10(0.1f));
  EXPECT_EQ(0x1f9a, osd.FloatToFixed3_10(-0.1f));
  // Test for maximum positive (<4)
  EXPECT_EQ(0x0FFF, osd.FloatToFixed3_10(4.0f));
  EXPECT_EQ(0x0FFF, osd.FloatToFixed3_10(40.0f));
  EXPECT_EQ(0x0FFF, osd.FloatToFixed3_10(3.9999f));
  // Test for minimum negative (>= -4)
  EXPECT_EQ(0x1000, osd.FloatToFixed3_10(-4.0f));
  EXPECT_EQ(0x1000, osd.FloatToFixed3_10(-14.0f));
}

TEST(AmlogicDisplay, FloatToFixed2_10) {
  inspect::Inspector inspector;
  amlogic_display::Osd osd = amlogic_display::Osd(100, 100, 100, 100, &inspector.GetRoot());
  EXPECT_EQ(0x0000, osd.FloatToFixed2_10(0.0f));
  EXPECT_EQ(0x0066, osd.FloatToFixed2_10(0.1f));
  EXPECT_EQ(0x0f9a, osd.FloatToFixed2_10(-0.1f));
  // Test for maximum positive (<2)
  EXPECT_EQ(0x07FF, osd.FloatToFixed2_10(2.0f));
  EXPECT_EQ(0x07FF, osd.FloatToFixed2_10(20.0f));
  EXPECT_EQ(0x07FF, osd.FloatToFixed2_10(1.9999f));
  // Test for minimum negative (>= -2)
  EXPECT_EQ(0x0800, osd.FloatToFixed2_10(-2.0f));
  EXPECT_EQ(0x0800, osd.FloatToFixed2_10(-14.0f));
}
