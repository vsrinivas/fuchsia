// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../image.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/pixelformat.h>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "../../fake/fake-display.h"
#include "../controller.h"
#include "../fence.h"
#include "base.h"

namespace display {

class ImageTest : public TestBase, public FenceCallback {
 public:
  void OnFenceFired(FenceReference* f) override {}
  void OnRefForFenceDead(Fence* fence) override { fence->OnRefDead(); }

  fbl::RefPtr<Image> ImportImage(zx::vmo&& vmo, image_t dc_image) {
    zx::vmo dup_vmo;
    EXPECT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo));
    // TODO: Factor this out of display::Client or make images easier to test without a client.
    if (display()->ImportVmoImage(&dc_image, std::move(vmo), /*offset=*/0) != ZX_OK) {
      return nullptr;
    }
    auto image = fbl::AdoptRef(new Image(controller(), dc_image, std::move(dup_vmo), /*stride=*/0));
    image->id = next_image_id_++;
    return image;
  }

 private:
  uint64_t next_image_id_ = 1;
};

TEST_F(ImageTest, MultipleAcquiresAllowed) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(1024 * 600 * 4, 0u, &vmo));
  image_t info = {};
  info.width = 1024;
  info.height = 600;
  info.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  auto image = ImportImage(std::move(vmo), info);

  EXPECT_TRUE(image->Acquire());
  image->DiscardAcquire();
  EXPECT_TRUE(image->Acquire());
  image->EarlyRetire();
}

TEST_F(ImageTest, RetiredImagesAreAlwaysUsable) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(1024 * 600 * 4, 0u, &vmo));
  image_t info = {};
  info.width = 1024;
  info.height = 600;
  info.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  info.type = 0;
  auto image = ImportImage(std::move(vmo), info);
  fbl::AutoCall image_cleanup([image]() {
    fbl::AutoLock l(image->mtx());
    image->ResetFences();
  });

  zx::event signal_event;
  ASSERT_OK(zx::event::create(0, &signal_event));
  zx::event signal_event_dup;
  signal_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &signal_event_dup);
  auto signal_fence = fbl::AdoptRef(
      new Fence(this, controller()->loop().dispatcher(), 1, std::move(signal_event_dup)));
  signal_fence->CreateRef();
  fbl::AutoCall signal_cleanup([signal_fence]() { signal_fence->ClearRef(); });

  zx::port signal_port;
  ASSERT_OK(zx::port::create(0, &signal_port));
  constexpr size_t kNumIterations = 1000;
  size_t failures = 0;
  size_t attempts = kNumIterations;
  size_t retire_count = 0;
  // Miniature naive render loop. Repeatedly acquire the image, run its lifecycle on another thread,
  // wait for the retirement fence, and try again.
  do {
    if (!image->Acquire()) {
      failures++;
      continue;
    }
    // Re-arm the event
    ASSERT_OK(signal_event.signal(ZX_EVENT_SIGNALED, 0));
    {
      fbl::AutoLock l(image->mtx());
      image->ResetFences();
      image->PrepareFences(nullptr, signal_fence->GetReference());
    }
    auto lifecycle_task = new async::Task(
        [image, &retire_count](async_dispatcher_t*, async::Task* task, zx_status_t) {
          fbl::AutoLock l(image->mtx());
          image->StartPresent();
          retire_count++;
          image->StartRetire();
          image->OnRetire();
          delete task;
        });
    EXPECT_OK(lifecycle_task->Post(controller()->loop().dispatcher()));
    signal_event.wait_async(signal_port, 0xfeed, ZX_EVENT_SIGNALED, 0);
    zx_port_packet_t packet;
    EXPECT_OK(signal_port.wait(zx::time::infinite(), &packet));
  } while (--attempts > 0);
  EXPECT_EQ(0, failures);
  EXPECT_EQ(kNumIterations, retire_count);
  {
    fbl::AutoLock l(image->mtx());
    image->ResetFences();
  }
  image->EarlyRetire();
}

}  // namespace display
