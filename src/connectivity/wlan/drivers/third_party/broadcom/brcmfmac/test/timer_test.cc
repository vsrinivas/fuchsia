/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/timer.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"

namespace wlan::brcmfmac {

struct TestTimerCfg {
  sync_completion_t wait_for_timer;
  brcmf_timer_info_t timer;
  WorkItem timeout_work;
  WorkQueue* queue;
  uint32_t target_cnt = 0;
  uint32_t timer_cnt = 0;
  bool call_timerset = false;
  bool call_timerstop = false;
  TestTimerCfg(async_dispatcher_t* dispatcher, WorkQueue* queue, uint32_t delay, uint32_t exp_count,
               bool periodic);
  ~TestTimerCfg();
};

class TimerTest : public testing::Test {
 public:
  TimerTest() {}

  void SetUp() override;
  async_dispatcher_t* GetDispatcher() { return dispatcher_loop_->dispatcher(); }
  WorkQueue* GetQueue() { return queue_.get(); }

 private:
  std::unique_ptr<async::Loop> dispatcher_loop_;
  std::unique_ptr<WorkQueue> queue_;
};

// Setup the dispatcher and work queue
void TimerTest::SetUp() {
  auto dispatcher_loop = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx_status_t status = dispatcher_loop->StartThread("test-timer-worker", nullptr);
  EXPECT_EQ(status, ZX_OK);
  dispatcher_loop_ = std::move(dispatcher_loop);
  auto queue = std::make_unique<WorkQueue>("TimerTest Work");
  queue_ = std::move(queue);
}

static void test_timer_handler(void* data) {
  struct TestTimerCfg* cfg = static_cast<decltype(cfg)>(data);
  cfg->queue->Schedule(&cfg->timeout_work);
}

static void test_timer_process(TestTimerCfg* cfg) {
  cfg->timer_cnt++;
  if (cfg->call_timerset) {
    brcmf_timer_set(&cfg->timer, cfg->timer.delay);
  }
  if (cfg->call_timerstop) {
    brcmf_timer_stop(&cfg->timer);
    sync_completion_signal(&cfg->wait_for_timer);
  }
  if (cfg->timer_cnt == cfg->target_cnt)
    sync_completion_signal(&cfg->wait_for_timer);
}

static void test_timeout_worker(WorkItem* work) {
  struct TestTimerCfg* cfg = containerof(work, struct TestTimerCfg, timeout_work);
  test_timer_process(cfg);
}

TestTimerCfg::TestTimerCfg(async_dispatcher_t* dispatcher, WorkQueue* q, uint32_t delay,
                           uint32_t exp_count, bool periodic) {
  timeout_work = WorkItem(test_timeout_worker);
  queue = q;
  target_cnt = exp_count;
  brcmf_timer_init(&timer, dispatcher, test_timer_handler, this, periodic);
  brcmf_timer_set(&timer, delay);
}

TestTimerCfg::~TestTimerCfg() { brcmf_timer_stop(&timer); }

// This test creates a one-shot timer and checks if the handler fired
TEST_F(TimerTest, one_shot) {
  TestTimerCfg timer(GetDispatcher(), GetQueue(), ZX_MSEC(10), 1, false);
  zx_status_t status = sync_completion_wait(&timer.wait_for_timer, ZX_TIME_INFINITE);
  // Check to make sure the timer fired
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(timer.timer_cnt, timer.target_cnt);
}

TEST_F(TimerTest, periodic) {
  TestTimerCfg timer(GetDispatcher(), GetQueue(), ZX_MSEC(25), 4, true);

  // Setup a second timer and ensure it runs ok too
  TestTimerCfg timer2(GetDispatcher(), GetQueue(), ZX_MSEC(50), 2, true);

  // Wait for the first timer to complete
  zx_status_t status = sync_completion_wait(&timer.wait_for_timer, ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);

  // and wait for the second timer to complete
  status = sync_completion_wait(&timer2.wait_for_timer, ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);

  // Check to make sure the timer fired exactly the expected # of times
  EXPECT_EQ(timer.timer_cnt, 4U);
  EXPECT_EQ(timer2.timer_cnt, 2U);
}

// This test creates a one-shot timer and checks if timer_set can be called
// from within the handler itself and a second timer is created to ensure
// calling timer_set() from within the handler does not have any side-effects
TEST_F(TimerTest, timerset_in_handler) {
  TestTimerCfg timer(GetDispatcher(), GetQueue(), ZX_MSEC(10), 2, false);
  timer.call_timerset = true;

  // Setup a second timer and ensure it runs ok too
  TestTimerCfg timer2(GetDispatcher(), GetQueue(), ZX_MSEC(25), 1, false);

  zx_status_t status = sync_completion_wait(&timer2.wait_for_timer, ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  // Check to make sure the first timer fired twice and the second once
  EXPECT_EQ(timer.timer_cnt, 2U);
  EXPECT_EQ(timer2.timer_cnt, 1U);
}

// This test creates a periodic timer and checks if timer_stop can be called
// from within the handler itself and a second timer is created to ensure
// calling timer_stop() from within the handler does not have any side-effects
TEST_F(TimerTest, timerstop_in_handler) {
  // Setup a periodic timer meant to fire twice
  TestTimerCfg timer(GetDispatcher(), GetQueue(), ZX_MSEC(10), 2, true);
  // but stop the timer after it fires once
  timer.call_timerstop = true;

  // Setup a second timer and ensure it runs ok too
  TestTimerCfg timer2(GetDispatcher(), GetQueue(), ZX_MSEC(10), 2, true);
  // wait until timer2 is done (sometime after 20 msecs)
  zx_status_t status = sync_completion_wait(&timer2.wait_for_timer, ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  status = sync_completion_wait(&timer.wait_for_timer, ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  // In about 20 msecs, first timer should have fired only once and second one twice
  EXPECT_EQ(timer.timer_cnt, 1U);
  EXPECT_EQ(timer2.timer_cnt, 2U);
}

}  // namespace wlan::brcmfmac
