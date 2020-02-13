// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-request-queue.h"

#include <lib/mmio/mmio.h>
#include <lib/mock-function/mock-function.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <array>

#include <ddk/protocol/usb/request.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <zxtest/zxtest.h>

#include "usb-transaction.h"

namespace mt_usb_hci {

// FakeTransaction is a Transaction with stub-functionality setup for testing.
class FakeTransaction : public Transaction {
 public:
  mock_function::MockFunction<size_t>& m_actual() { return m_actual_; }
  mock_function::MockFunction<void, bool>& m_advance() { return m_advance_; }
  mock_function::MockFunction<bool>& m_ok() { return m_ok_; }
  mock_function::MockFunction<void>& m_cancel() { return m_cancel_; }
  mock_function::MockFunction<void>& m_wait() { return m_wait_; }

  size_t actual() const { return m_actual_.Call(); }
  void Advance(bool interrupt) { return m_advance_.Call(interrupt); }
  bool Ok() const { return m_ok_.Call(); }
  void Cancel() { return m_cancel_.Call(); }
  void Wait() { return m_wait_.Call(); }

 private:
  mutable mock_function::MockFunction<size_t> m_actual_;
  mock_function::MockFunction<void, bool> m_advance_;
  mutable mock_function::MockFunction<bool> m_ok_;
  mock_function::MockFunction<void> m_cancel_;
  mock_function::MockFunction<void> m_wait_;
};

// TestingQueue is a class with stub DispatchRequest().
class TestingQueue : public TransactionQueue {
 public:
  explicit TestingQueue(ddk::MmioView view) : TransactionQueue(view, 123, {}), dispatch_ct_(0) {}

  mock_function::MockFunction<zx_status_t>& m_dispatch() { return m_dispatch_; }

  FakeTransaction& transaction() { return static_cast<FakeTransaction&>(*transaction_); }
  void new_transaction() { transaction_.reset(new FakeTransaction); }

  // Wait for n-invocations of DispatchRequest() to be invoked.  This allows us to synchronize the
  // tests to the iterations of the queue thread.
  void Wait(int n = 1) {
    fbl::AutoLock _(&lock_);
    while (dispatch_ct_ < n) {
      cond_.Wait(&lock_);
    }
  }

 private:
  zx_status_t DispatchRequest(usb::BorrowedRequest<> req) override {
    fbl::AutoLock _(&lock_);
    dispatch_ct_++;
    cond_.Signal();
    req.Complete(ZX_OK, 0);
    return m_dispatch_.Call();
  }

  // The usb::BorrowedRequest supplied to TransactionEndpoint::DispatchRequest is being ignored
  // here because MockFunction::Call doesn't implement perfect forwarding.
  mock_function::MockFunction<zx_status_t> m_dispatch_;
  int dispatch_ct_ TA_GUARDED(lock_);

  fbl::Mutex lock_;
  fbl::ConditionVariable cond_ TA_GUARDED(lock_);
};

class TransactionQueueTest : public zxtest::Test {
 protected:
  void SetUp() {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(4096, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, 4096, std::move(vmo), ZX_CACHE_POLICY_CACHED, &mmio_));
  }

  std::optional<ddk::MmioBuffer> mmio_;

  static constexpr usb_request_complete_t cb_ = {
      .callback = [](void*, usb_request_t* r) { usb::Request<>(r, sizeof(usb_request_t)); },
      .ctx = nullptr,
  };
};

TEST_F(TransactionQueueTest, QueueThread_StartAndHalt) {
  TestingQueue q(mmio_->View(0));
  EXPECT_OK(q.StartQueueThread());
  EXPECT_OK(q.Halt());
}

TEST_F(TransactionQueueTest, QueueThread_Enqueue) {
  std::optional<usb::Request<>> o_req;
  size_t alloc_sz = usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
  ASSERT_OK(usb::Request<>::Alloc(&o_req, 4096, 0, alloc_sz));
  usb::BorrowedRequest<> req(o_req->take(), cb_, sizeof(usb_request_t));

  TestingQueue q(mmio_->View(0));
  q.m_dispatch().ExpectCall(ZX_OK);
  EXPECT_OK(q.StartQueueThread());
  EXPECT_OK(q.QueueRequest(std::move(req)));
  q.Wait();

  EXPECT_OK(q.Halt());
  q.m_dispatch().VerifyAndClear();
}

TEST_F(TransactionQueueTest, QueueThread_EnqueueMultiBeforeThreadStarts) {
  std::optional<usb::Request<>> o_req1;
  std::optional<usb::Request<>> o_req2;
  std::optional<usb::Request<>> o_req3;
  std::optional<usb::Request<>> o_req4;
  std::optional<usb::Request<>> o_req5;

  size_t alloc_sz = usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
  ASSERT_OK(usb::Request<>::Alloc(&o_req1, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req2, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req3, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req4, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req5, 4096, 0, alloc_sz));

  usb::BorrowedRequest<> req1(o_req1->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req2(o_req2->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req3(o_req3->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req4(o_req4->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req5(o_req5->take(), cb_, sizeof(usb_request_t));

  TestingQueue q(mmio_->View(0));
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);

  EXPECT_OK(q.QueueRequest(std::move(req1)));
  EXPECT_OK(q.QueueRequest(std::move(req2)));
  EXPECT_OK(q.QueueRequest(std::move(req3)));
  EXPECT_OK(q.QueueRequest(std::move(req4)));
  EXPECT_OK(q.QueueRequest(std::move(req5)));
  EXPECT_OK(q.StartQueueThread());
  q.Wait(5);

  EXPECT_OK(q.Halt());
  q.m_dispatch().VerifyAndClear();
}

TEST_F(TransactionQueueTest, QueueThread_EnqueueMultiAfterThreadStarts) {
  std::optional<usb::Request<>> o_req1;
  std::optional<usb::Request<>> o_req2;
  std::optional<usb::Request<>> o_req3;
  std::optional<usb::Request<>> o_req4;
  std::optional<usb::Request<>> o_req5;

  size_t alloc_sz = usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
  ASSERT_OK(usb::Request<>::Alloc(&o_req1, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req2, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req3, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req4, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req5, 4096, 0, alloc_sz));

  usb::BorrowedRequest<> req1(o_req1->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req2(o_req2->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req3(o_req3->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req4(o_req4->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req5(o_req5->take(), cb_, sizeof(usb_request_t));

  TestingQueue q(mmio_->View(0));
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);

  EXPECT_OK(q.StartQueueThread());
  EXPECT_OK(q.QueueRequest(std::move(req1)));
  EXPECT_OK(q.QueueRequest(std::move(req2)));
  EXPECT_OK(q.QueueRequest(std::move(req3)));
  EXPECT_OK(q.QueueRequest(std::move(req4)));
  EXPECT_OK(q.QueueRequest(std::move(req5)));
  q.Wait(5);

  EXPECT_OK(q.Halt());
  q.m_dispatch().VerifyAndClear();
}

TEST_F(TransactionQueueTest, QueueThread_EnqueueMultiDuringThreadStart) {
  std::optional<usb::Request<>> o_req1;
  std::optional<usb::Request<>> o_req2;
  std::optional<usb::Request<>> o_req3;
  std::optional<usb::Request<>> o_req4;
  std::optional<usb::Request<>> o_req5;

  size_t alloc_sz = usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
  ASSERT_OK(usb::Request<>::Alloc(&o_req1, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req2, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req3, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req4, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req5, 4096, 0, alloc_sz));

  usb::BorrowedRequest<> req1(o_req1->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req2(o_req2->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req3(o_req3->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req4(o_req4->take(), cb_, sizeof(usb_request_t));
  usb::BorrowedRequest<> req5(o_req5->take(), cb_, sizeof(usb_request_t));

  TestingQueue q(mmio_->View(0));
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);
  q.m_dispatch().ExpectCall(ZX_OK);

  EXPECT_OK(q.QueueRequest(std::move(req1)));
  EXPECT_OK(q.QueueRequest(std::move(req2)));
  EXPECT_OK(q.StartQueueThread());
  EXPECT_OK(q.QueueRequest(std::move(req3)));
  EXPECT_OK(q.QueueRequest(std::move(req4)));
  EXPECT_OK(q.QueueRequest(std::move(req5)));
  q.Wait(5);

  EXPECT_OK(q.Halt());
  q.m_dispatch().VerifyAndClear();
}

TEST_F(TransactionQueueTest, QueueThread_CancelAll) {
  typedef struct {
    int idx = 0;
    std::array<zx_status_t, 5> array;
  } capture_t;

  auto callback = [](void* ctx, usb_request_t* req) {
    auto* cap = static_cast<capture_t*>(ctx);
    cap->array[cap->idx++] = req->response.status;
    usb::Request<>(req, sizeof(usb_request_t));
  };

  capture_t cap;
  const usb_request_complete_t cb = {
      .callback = callback,
      .ctx = &cap,
  };

  std::optional<usb::Request<>> o_req1;
  std::optional<usb::Request<>> o_req2;
  std::optional<usb::Request<>> o_req3;
  std::optional<usb::Request<>> o_req4;
  std::optional<usb::Request<>> o_req5;

  size_t alloc_sz = usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
  ASSERT_OK(usb::Request<>::Alloc(&o_req1, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req2, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req3, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req4, 4096, 0, alloc_sz));
  ASSERT_OK(usb::Request<>::Alloc(&o_req5, 4096, 0, alloc_sz));

  usb::BorrowedRequest<> req1(o_req1->take(), cb, sizeof(usb_request_t));
  usb::BorrowedRequest<> req2(o_req2->take(), cb, sizeof(usb_request_t));
  usb::BorrowedRequest<> req3(o_req3->take(), cb, sizeof(usb_request_t));
  usb::BorrowedRequest<> req4(o_req4->take(), cb, sizeof(usb_request_t));
  usb::BorrowedRequest<> req5(o_req5->take(), cb, sizeof(usb_request_t));

  // Note here we don't start the thread.
  TestingQueue q(mmio_->View(0));
  q.new_transaction();
  q.transaction().m_cancel().ExpectCall();
  EXPECT_OK(q.QueueRequest(std::move(req1)));
  EXPECT_OK(q.QueueRequest(std::move(req2)));
  EXPECT_OK(q.QueueRequest(std::move(req3)));
  EXPECT_OK(q.QueueRequest(std::move(req4)));
  EXPECT_OK(q.QueueRequest(std::move(req5)));
  EXPECT_OK(q.CancelAll());

  EXPECT_EQ(ZX_ERR_CANCELED, cap.array[0]);
  EXPECT_EQ(ZX_ERR_CANCELED, cap.array[1]);
  EXPECT_EQ(ZX_ERR_CANCELED, cap.array[2]);
  EXPECT_EQ(ZX_ERR_CANCELED, cap.array[3]);
  EXPECT_EQ(ZX_ERR_CANCELED, cap.array[4]);
  q.m_dispatch().VerifyAndClear();
  q.transaction().m_cancel().VerifyAndClear();
}

}  // namespace mt_usb_hci

int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
