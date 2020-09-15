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

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"

namespace wlan::brcmfmac {

struct TestTimerCfg {
  sync_completion_t wait_for_timer;
  Timer* timer = nullptr;
  WorkItem timeout_work;
  WorkQueue* queue;
  uint32_t delay;
  uint32_t target_cnt = 0;
  uint32_t timer_exc_cnt = 0;
  uint32_t timer_sch_cnt = 0;
  bool call_timerset = false;
  bool call_timerstop = false;
  bool periodic = false;
  char name[32];
  TestTimerCfg(const char* name);
  ~TestTimerCfg();
};

class TimerTest : public testing::Test {
 public:
  TimerTest() {}
  ~TimerTest();

  void SetUp() override;
  brcmf_pub* GetFakeDrvr() { return fake_drvr_.get(); }
  WorkQueue* GetQueue() { return queue_.get(); }
  void CreateTimer(TestTimerCfg* cfg, uint32_t exp_count, bool periodic, bool call_timerset,
                   bool call_timerstop);
  void StartTimer(TestTimerCfg* cfg, uint32_t delay);
  void StopTimer(TestTimerCfg* cfg);
  zx_status_t WaitForTimer(TestTimerCfg* cfg);
  TestTimerCfg* GetTimerCfg1() { return timer_cfg_1_.get(); }
  TestTimerCfg* GetTimerCfg2() { return timer_cfg_2_.get(); }

 private:
  std::unique_ptr<WorkQueue> queue_;
  std::unique_ptr<brcmf_pub> fake_drvr_;
  std::unique_ptr<async::Loop> dispatcher_loop_;
  std::unique_ptr<TestTimerCfg> timer_cfg_1_;
  std::unique_ptr<TestTimerCfg> timer_cfg_2_;
};

// Setup the dispatcher and work queue
void TimerTest::SetUp() {
  fake_drvr_ = std::make_unique<brcmf_pub>();
  dispatcher_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx_status_t status = dispatcher_loop_->StartThread("test-timer-worker", nullptr);
  EXPECT_EQ(status, ZX_OK);
  fake_drvr_->dispatcher = dispatcher_loop_->dispatcher();
  queue_ = std::make_unique<WorkQueue>("TimerTest Work");
  timer_cfg_1_ = std::make_unique<TestTimerCfg>("timer1");
  timer_cfg_2_ = std::make_unique<TestTimerCfg>("timer2");
  // The timer must be standard layout in order to use containerof
  ASSERT_TRUE(std::is_standard_layout<Timer>::value);
}

TimerTest::~TimerTest() {
  queue_->Flush();
  if (dispatcher_loop_ != nullptr) {
    dispatcher_loop_->Shutdown();
  }
}

static void test_timer_handler(void* data) {
  struct TestTimerCfg* cfg = static_cast<decltype(cfg)>(data);
  if (cfg->timer_sch_cnt < cfg->target_cnt)
    cfg->queue->Schedule(&cfg->timeout_work);
  cfg->timer_sch_cnt++;
  if (cfg->call_timerstop) {
    cfg->timer->Stop();
  }
}

static void test_timer_process(TestTimerCfg* cfg) {
  // There shouldn't be a race between the running tests and the async tasks lapping over
  cfg->timer_exc_cnt++;
  if (cfg->timer_exc_cnt > cfg->target_cnt)
    return;

  if (cfg->call_timerset) {
    cfg->timer->Start(cfg->delay);
  }
  if (cfg->call_timerstop || (cfg->timer_exc_cnt == cfg->target_cnt)) {
    sync_completion_signal(&cfg->wait_for_timer);
  }
}

static void test_timeout_worker(WorkItem* work) {
  struct TestTimerCfg* cfg = containerof(work, struct TestTimerCfg, timeout_work);
  test_timer_process(cfg);
}

TestTimerCfg::TestTimerCfg(const char* timer_name) { strcpy(name, timer_name); }

TestTimerCfg::~TestTimerCfg() {
  if (timer) {
    timer->Stop();
    delete timer;
  }
}

void TimerTest::CreateTimer(TestTimerCfg* cfg, uint32_t exp_count, bool periodic,
                            bool call_timerset, bool call_timerstop) {
  cfg->timeout_work = WorkItem(test_timeout_worker);
  cfg->queue = GetQueue();
  cfg->target_cnt = exp_count;
  cfg->call_timerset = call_timerset;
  cfg->call_timerstop = call_timerstop;
  cfg->timer = new Timer(GetFakeDrvr(), std::bind(test_timer_handler, cfg), periodic);
}

void TimerTest::StartTimer(TestTimerCfg* cfg, uint32_t delay) {
  ZX_ASSERT(cfg->timer);
  cfg->delay = delay;
  cfg->timer->Start(delay);
}

void TimerTest::StopTimer(TestTimerCfg* cfg) {
  ZX_ASSERT(cfg->timer);
  cfg->timer->Stop();
}

zx_status_t TimerTest::WaitForTimer(TestTimerCfg* cfg) {
  return sync_completion_wait(&cfg->wait_for_timer, ZX_TIME_INFINITE);
}
// This test creates a one-shot timer and checks if the handler fired
TEST_F(TimerTest, one_shot) {
  auto timer = GetTimerCfg1();
  CreateTimer(timer, 1, false, false, false);
  StartTimer(timer, ZX_MSEC(10));
  zx_status_t status = WaitForTimer(timer);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(timer->timer_exc_cnt, timer->target_cnt);
  EXPECT_EQ(timer->timer->Stopped(), true);
}

TEST_F(TimerTest, periodic) {
  auto timer1 = GetTimerCfg1();
  auto timer2 = GetTimerCfg2();
  CreateTimer(timer1, 4, true, false, false);
  CreateTimer(timer2, 2, true, false, false);
  StartTimer(timer1, ZX_MSEC(25));
  StartTimer(timer2, ZX_MSEC(50));

  // Wait for the first timer to complete
  zx_status_t status = WaitForTimer(timer1);
  timer1->timer->Stop();
  EXPECT_EQ(status, ZX_OK);

  // and wait for the second timer to complete
  status = WaitForTimer(timer2);
  timer2->timer->Stop();
  EXPECT_EQ(status, ZX_OK);

  // Check to make sure the timer fired exactly the expected # of times
  EXPECT_EQ(timer1->timer_exc_cnt, 4U);
  EXPECT_EQ(timer2->timer_exc_cnt, 2U);
  EXPECT_EQ(timer1->timer->Stopped(), true);
  EXPECT_EQ(timer2->timer->Stopped(), true);
}

// This test creates a one-shot timer and checks if timer_set can be called
// from within the handler itself and a second timer is created to ensure
// calling timer_set() from within the handler does not have any side-effects
TEST_F(TimerTest, timerset_in_handler) {
  // A timerset will be called inside callback
  auto timer1 = GetTimerCfg1();
  auto timer2 = GetTimerCfg2();
  CreateTimer(timer1, 2, false, true, false);
  CreateTimer(timer2, 1, false, false, false);
  StartTimer(timer1, ZX_MSEC(10));
  StartTimer(timer2, ZX_MSEC(25));

  // Wait for the first timer to complete
  zx_status_t status = WaitForTimer(timer1);
  timer1->timer->Stop();
  EXPECT_EQ(status, ZX_OK);

  // and wait for the second timer to complete
  status = WaitForTimer(timer2);
  timer2->timer->Stop();
  EXPECT_EQ(status, ZX_OK);

  // Check to make sure the timer fired exactly the expected # of times
  EXPECT_EQ(timer1->timer_exc_cnt, 2U);
  EXPECT_EQ(timer2->timer_exc_cnt, 1U);
  EXPECT_EQ(timer1->timer->Stopped(), true);
  EXPECT_EQ(timer2->timer->Stopped(), true);
}

// This test creates a periodic timer and checks if timer_stop can be called
// from within the handler itself and a second timer is created to ensure
// calling timer_stop() from within the handler does not have any side-effects
TEST_F(TimerTest, timerstop_in_handler) {
  // Setup a periodic timer meant to fire twice but a timerstop will be called inside callback
  auto timer1 = GetTimerCfg1();
  auto timer2 = GetTimerCfg2();
  CreateTimer(timer1, 5, true, false, true);
  CreateTimer(timer2, 2, true, false, false);
  StartTimer(timer1, ZX_MSEC(10));
  StartTimer(timer2, ZX_MSEC(10));

  // Wait for the first timer to complete
  zx_status_t status = WaitForTimer(timer2);
  timer2->timer->Stop();
  EXPECT_EQ(status, ZX_OK);

  // and wait for the second timer to complete
  status = WaitForTimer(timer1);
  timer1->timer->Stop();
  EXPECT_EQ(status, ZX_OK);

  // In about 20 msecs, first timer should have fired at least once and second one twice
  // Since the timer handler runs on the Work Q task, it is likely that the main
  // Timer handler runs to completion (after adding the Work Item) before the timer
  // handler runs. Because of this the main Timer handler might arm the timer again
  // before the timer is stopped (by the timer handler).
  // TODO: Does the handler need to be added to the Work Q?
  EXPECT_GT(timer1->timer_exc_cnt, 0U);
  EXPECT_EQ(timer2->timer_exc_cnt, 2U);
  EXPECT_EQ(timer1->timer->Stopped(), true);
  EXPECT_EQ(timer2->timer->Stopped(), true);
}

}  // namespace wlan::brcmfmac
