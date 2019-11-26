// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/cpp/paged_vmo.h>

#include <zxtest/zxtest.h>

namespace {

const zx_port_packet_t dummy_port_packet{
    .key = 0,
    .type = 0,
    .status = ZX_OK,
    .page_request =
        {
            .command = 0u,
            .flags = 0u,
            .reserved0 = 0u,
            .offset = 0u,
            .length = 0u,
            .reserved1 = 0u,
        },
};

class MockDispatcher : public async::DispatcherStub {
 public:
  enum class Op {
    NONE,
    CREATE,
    DETACH,
  };

  zx_status_t CreatePagedVmo(async_paged_vmo_t* paged_vmo, zx_handle_t pager, uint32_t options,
                             uint64_t vmo_size, zx_handle_t* vmo_out) override {
    last_op = Op::CREATE;
    last_paged_vmo = paged_vmo;
    last_options = options;
    last_vmo_size = vmo_size;

    // This isn't *actually* a paged vmo, but these tests are just testing plumbing through
    // ulib/async, not the internal pager API, so this shouldn't be an issue.
    //
    // Propagate a real value back so the caller can safely close it.
    zx::vmo vmo;
    zx::vmo::create(8192, 0, &vmo);
    *vmo_out = vmo.release();
    return next_status;
  }

  zx_status_t DetachPagedVmo(async_paged_vmo_t* paged_vmo) override {
    last_op = Op::DETACH;
    last_paged_vmo = paged_vmo;
    return next_status;
  }

  Op last_op = Op::NONE;
  async_paged_vmo_t* last_paged_vmo = nullptr;
  uint32_t last_options = 0;
  uint64_t last_vmo_size = 0;
  zx_status_t next_status = ZX_OK;
};

class Harness {
 public:
  void Handler(async_dispatcher_t* dispatcher, async::PagedVmoBase* paged_vmo, zx_status_t status,
               const zx_packet_page_request_t* request) {
    handler_ran = true;
    last_paged_vmo = paged_vmo;
    last_status = status;
    last_request = request;
  }

  virtual async::PagedVmoBase& paged_vmo() = 0;

  bool handler_ran = false;
  async::PagedVmoBase* last_paged_vmo = nullptr;
  zx_status_t last_status = ZX_ERR_INTERNAL;
  const zx_packet_page_request_t* last_request = nullptr;
};

class LambdaHarness : public Harness {
 public:
  LambdaHarness()
      : paged_vmo_{[this](async_dispatcher_t* dispatcher, async::PagedVmoBase* paged_vmo,
                          zx_status_t status, const zx_packet_page_request_t* request) {
          Handler(dispatcher, paged_vmo, status, request);
        }} {}

  async::PagedVmoBase& paged_vmo() override { return paged_vmo_; }

 private:
  async::PagedVmo paged_vmo_;
};

class MethodHarness : public Harness {
 public:
  MethodHarness() : paged_vmo_{this} {}

  async::PagedVmoBase& paged_vmo() override { return paged_vmo_; }

 private:
  async::PagedVmoMethod<Harness, &Harness::Handler> paged_vmo_;
};

void InitializeUnboundTest(Harness* harness) {
  MockDispatcher dispatcher;
  EXPECT_FALSE(harness->paged_vmo().is_bound());
  EXPECT_STATUS(ZX_ERR_NOT_FOUND, harness->paged_vmo().Detach());
}

TEST(PagedVmoLambdaTest, InitializedUnbound) {
  LambdaHarness harness;
  InitializeUnboundTest(&harness);
}

TEST(PagedVmoMethodTest, InitializedUnbound) {
  MethodHarness harness;
  InitializeUnboundTest(&harness);
}

void CreateVmoThenDetachTest(Harness* harness) {
  MockDispatcher dispatcher;
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);
  uint32_t options = 1;
  uint64_t vmo_size = 2;
  zx::vmo vmo;
  ASSERT_OK(harness->paged_vmo().CreateVmo(&dispatcher, zx::unowned_pager(pager.get()), options,
                                           vmo_size, &vmo));
  EXPECT_EQ(MockDispatcher::Op::CREATE, dispatcher.last_op);
  EXPECT_EQ(options, dispatcher.last_options);
  EXPECT_EQ(vmo_size, dispatcher.last_vmo_size);
  EXPECT_TRUE(vmo);
  EXPECT_FALSE(harness->handler_ran);

  EXPECT_OK(harness->paged_vmo().Detach());
  EXPECT_EQ(MockDispatcher::Op::DETACH, dispatcher.last_op);
  EXPECT_EQ(pager.get(), dispatcher.last_paged_vmo->pager);
  EXPECT_EQ(vmo.get(), dispatcher.last_paged_vmo->vmo);
}

TEST(PagedVmoLambdaTest, CreateVmoThenDetach) {
  LambdaHarness harness;
  CreateVmoThenDetachTest(&harness);
}

TEST(PagedVmoMethodTest, CreateVmoThenDetach) {
  MethodHarness harness;
  CreateVmoThenDetachTest(&harness);
}

void RepeatedCreationTest(Harness* harness) {
  MockDispatcher dispatcher;
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);
  uint32_t options = 1;
  uint64_t vmo_size = 2;
  zx::vmo vmo;
  // Repeated creation fails.
  ASSERT_OK(harness->paged_vmo().CreateVmo(&dispatcher, zx::unowned_pager(pager.get()), options,
                                           vmo_size, &vmo));
  EXPECT_STATUS(ZX_ERR_ALREADY_EXISTS,
                harness->paged_vmo().CreateVmo(&dispatcher, zx::unowned_pager(pager.get()), options,
                                               vmo_size, &vmo));

  // Creation after detaching succeeds.
  ASSERT_OK(harness->paged_vmo().Detach());
  EXPECT_OK(harness->paged_vmo().CreateVmo(&dispatcher, zx::unowned_pager(pager.get()), options,
                                           vmo_size, &vmo));
}

TEST(PagedVmoLambdaTest, RepeatedVmoCreation) {
  LambdaHarness harness;
  RepeatedCreationTest(&harness);
}

TEST(PagedVmoMethodTest, RepeatedVmoCreation) {
  MethodHarness harness;
  RepeatedCreationTest(&harness);
}

void RunHandlerTest(Harness* harness) {
  MockDispatcher dispatcher;
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);
  uint32_t options = 1;
  uint64_t vmo_size = 2;
  zx::vmo vmo;
  ASSERT_OK(harness->paged_vmo().CreateVmo(&dispatcher, zx::unowned_pager(pager.get()), options,
                                           vmo_size, &vmo));

  ASSERT_FALSE(harness->handler_ran);
  dispatcher.last_paged_vmo->handler(&dispatcher, dispatcher.last_paged_vmo, ZX_OK,
                                     &dummy_port_packet.page_request);
  EXPECT_TRUE(harness->handler_ran);
  EXPECT_EQ(&harness->paged_vmo(), harness->last_paged_vmo);
  EXPECT_STATUS(ZX_OK, harness->last_status);
  EXPECT_EQ(&dummy_port_packet.page_request, harness->last_request);
}

TEST(PagedVmoLambdaTest, RunHandler) {
  LambdaHarness harness;
  RunHandlerTest(&harness);
}

TEST(PagedVmoMethodTest, RunHandler) {
  MethodHarness harness;
  RunHandlerTest(&harness);
}

TEST(PagedVmoStubsTest, CreateVmoStub) {
  async::DispatcherStub dispatcher;
  async_paged_vmo_t paged_vmo = {};
  zx_handle_t dummy_vmo;
  EXPECT_STATUS(ZX_ERR_NOT_SUPPORTED, async_create_paged_vmo(&dispatcher, &paged_vmo, 0,
                                                             ZX_HANDLE_INVALID, 0, &dummy_vmo));
}

TEST(PagedVmoStubsTest, DetachStub) {
  async::DispatcherStub dispatcher;
  async_paged_vmo_t paged_vmo = {};
  EXPECT_STATUS(ZX_ERR_NOT_SUPPORTED, async_detach_paged_vmo(&dispatcher, &paged_vmo));
}

void CanceledUnboundTest(Harness* harness) {
  MockDispatcher dispatcher;
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);
  uint32_t options = 1;
  uint64_t vmo_size = 2;
  zx::vmo vmo;
  ASSERT_OK(harness->paged_vmo().CreateVmo(&dispatcher, zx::unowned_pager(pager.get()), options,
                                           vmo_size, &vmo));

  ASSERT_FALSE(harness->handler_ran);
  dispatcher.last_paged_vmo->handler(&dispatcher, dispatcher.last_paged_vmo, ZX_ERR_CANCELED,
                                     nullptr);
  EXPECT_TRUE(harness->handler_ran);
  EXPECT_EQ(&harness->paged_vmo(), harness->last_paged_vmo);
  EXPECT_STATUS(ZX_ERR_CANCELED, harness->last_status);
  EXPECT_EQ(nullptr, harness->last_request);
  EXPECT_FALSE(harness->paged_vmo().is_bound());
}

TEST(PagedVmoLambdaTest, CanceledUnbound) {
  LambdaHarness harness;
  CanceledUnboundTest(&harness);
}

TEST(PagedVmoMethodTest, CanceledUnbound) {
  MethodHarness harness;
  CanceledUnboundTest(&harness);
}

}  // namespace
