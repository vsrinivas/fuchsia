// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/fidl_allocator.h>
#include <lib/fidl/llcpp/object_view.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <cstdint>

#include <gtest/gtest.h>
#include <llcpptest/handleraii/test/llcpp/fidl.h>
#include <src/lib/fidl/llcpp/tests/types_test_utils.h>

namespace test = ::llcpptest_handleraii_test;

// All the tests in this file check that when a result is freed, all the handles inside the result
// are closed.

class HandleCloseProviderServer : public test::HandleProvider::Interface {
 public:
  void GetHandle(GetHandleCompleter::Sync& completer) override {
    zx::event e;
    zx::event::create(0, &e);
    completer.Reply(std::move(e));
  }
  void GetHandleStruct(GetHandleStructCompleter::Sync& completer) override {
    test::wire::HandleStruct s;
    zx::event::create(0, &s.h);
    completer.Reply(std::move(s));
  }
  void GetHandleStructStruct(GetHandleStructStructCompleter::Sync& completer) override {
    test::wire::HandleStructStruct s;
    zx::event::create(0, &s.s.h);
    completer.Reply(std::move(s));
  }
  void GetMultiFieldStruct(GetMultiFieldStructCompleter::Sync& completer) override {
    test::wire::MultiFieldStruct s;
    zx::event::create(0, &s.h1);
    zx::event::create(0, &s.s.h);
    zx::event::create(0, &s.h2);
    completer.Reply(std::move(s));
  }
  void GetMultiArgs(GetMultiArgsCompleter::Sync& completer) override {
    zx::event h1;
    zx::event::create(0, &h1);
    test::wire::HandleStruct s;
    zx::event::create(0, &s.h);
    zx::event h2;
    zx::event::create(0, &h2);
    completer.Reply(std::move(h1), std::move(s), std::move(h2));
  }
  void GetVectorStruct(uint32_t count, GetVectorStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<test::wire::HandleStruct> v(allocator, count);
    for (auto& s : v) {
      zx::event::create(0, &s.h);
    }
    test::wire::VectorStruct s;
    s.v = std::move(v);
    completer.Reply(std::move(s));
  }
  void GetArrayStruct(GetArrayStructCompleter::Sync& completer) override {
    test::wire::ArrayStruct s;
    for (size_t i = 0; i < s.a.size(); ++i) {
      zx::event::create(0, &s.a[i].h);
    }
    completer.Reply(std::move(s));
  }
  void GetHandleUnion(int32_t field, GetHandleUnionCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleUnion u;
    if (field == 1) {
      u.set_h1(allocator);
      zx::event::create(0, &u.mutable_h1());
    } else if (field == 2) {
      u.set_h2(allocator);
      zx::event::create(0, &u.mutable_h2().h);
    }
    completer.Reply(std::move(u));
  }
  void GetHandleUnionStruct(int32_t field,
                            GetHandleUnionStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleUnionStruct u;
    if (field == 1) {
      zx::event event;
      zx::event::create(0, &event);
      u.u.set_h1(allocator, std::move(event));
    } else if (field == 2) {
      u.u.set_h2(allocator);
      zx::event::create(0, &u.u.mutable_h2().h);
    }
    completer.Reply(std::move(u));
  }
  void GetHandleTable(uint32_t fields, GetHandleTableCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleTable t(allocator);
    if ((fields & 1) != 0) {
      zx::event event;
      zx::event::create(0, &event);
      t.set_h1(allocator, std::move(event));
    }
    if ((fields & 2) != 0) {
      t.set_h2(allocator);
      zx::event::create(0, &t.h2().h);
    }
    completer.Reply(std::move(t));
  }
  void GetHandleTableStruct(uint32_t fields,
                            GetHandleTableStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleTableStruct reply;
    reply.t.Allocate(allocator);
    if ((fields & 1) != 0) {
      zx::event event;
      zx::event::create(0, &event);
      reply.t.set_h1(allocator, std::move(event));
    }
    if ((fields & 2) != 0) {
      reply.t.set_h2(allocator);
      zx::event::create(0, &reply.t.h2().h);
    }
    completer.Reply(std::move(reply));
  }
  void GetOptionalHandleStruct(bool defined,
                               GetOptionalHandleStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    if (defined) {
      fidl::ObjectView<test::wire::HandleStruct> s(allocator);
      zx::event::create(0, &s->h);
      completer.Reply(s);
    } else {
      completer.Reply(nullptr);
    }
  }
  void GetOptionalHandleUnion(int32_t field,
                              GetOptionalHandleUnionCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleUnion u;
    if (field == 1) {
      zx::event event;
      zx::event::create(0, &event);
      u.set_h1(allocator, std::move(event));
    } else if (field == 2) {
      u.set_h2(allocator);
      zx::event::create(0, &u.mutable_h2().h);
    }
    completer.Reply(std::move(u));
  }
  void GetOptionalHandleUnionStruct(
      bool defined, int32_t field,
      GetOptionalHandleUnionStructCompleter::Sync& completer) override {
    if (defined) {
      fidl::FidlAllocator allocator;
      fidl::ObjectView<test::wire::HandleUnionStruct> u(allocator);
      if (field == 1) {
        zx::event event;
        zx::event::create(0, &event);
        u->u.set_h1(allocator, std::move(event));
      } else if (field == 2) {
        u->u.set_h2(allocator);
        zx::event::create(0, &u->u.mutable_h2().h);
      }
      completer.Reply(u);
    } else {
      completer.Reply(nullptr);
    }
  }
  void GetOptionalHandleTableStruct(
      bool defined, uint32_t fields,
      GetOptionalHandleTableStructCompleter::Sync& completer) override {
    if (defined) {
      fidl::FidlAllocator allocator;
      fidl::ObjectView<test::wire::HandleTableStruct> reply(allocator);
      reply->t.Allocate(allocator);
      if ((fields & 1) != 0) {
        fidl::ObjectView<zx::event> e(allocator);
        zx::event::create(0, e.get());
        reply->t.set_h1(e);
      }
      if ((fields & 2) != 0) {
        fidl::ObjectView<test::wire::HandleStruct> s(allocator);
        zx::event::create(0, &s->h);
        reply->t.set_h2(s);
      }
      completer.Reply(reply);
    } else {
      completer.Reply(nullptr);
    }
  }
  void GetHandleStructOptionalStruct(
      bool defined, GetHandleStructOptionalStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleStructOptionalStruct reply;
    if (defined) {
      fidl::ObjectView<test::wire::HandleStruct> s(allocator);
      zx::event::create(0, &s->h);
      reply.s = s;
    }
    completer.Reply(std::move(reply));
  }
  void GetHandleUnionOptionalStruct(
      bool defined, int32_t field,
      GetHandleUnionOptionalStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    test::wire::HandleUnionOptionalStruct reply;
    if (defined) {
      if (field == 1) {
        zx::event event;
        zx::event::create(0, &event);
        reply.u.set_h1(allocator, std::move(event));
      } else if (field == 2) {
        fidl::ObjectView<test::wire::HandleStruct> s(allocator);
        reply.u.set_h2(allocator);
        zx::event::create(0, &reply.u.mutable_h2().h);
      }
    }
    completer.Reply(std::move(reply));
  }
  void GetVectorOfHandle(uint32_t count, GetVectorOfHandleCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<zx::event> v(allocator, count);
    for (auto& item : v) {
      zx::event::create(0, &item);
    }
    completer.Reply(std::move(v));
  }
  void GetVectorOfVectorOfHandle(uint32_t count1, uint32_t count2,
                                 GetVectorOfVectorOfHandleCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<fidl::VectorView<zx::event>> v(allocator, count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1].Allocate(allocator, count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        zx::event::create(0, &v[i1][i2]);
      }
    }
    completer.Reply(std::move(v));
  }
  void GetVectorOfVectorOfVectorOfHandle(
      uint32_t count1, uint32_t count2, uint32_t count3,
      GetVectorOfVectorOfVectorOfHandleCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<fidl::VectorView<fidl::VectorView<zx::event>>> v(allocator, count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1].Allocate(allocator, count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        v[i1][i2].Allocate(allocator, count3);
        for (uint32_t i3 = 0; i3 < count3; ++i3) {
          zx::event::create(0, &v[i1][i2][i3]);
        }
      }
    }
    completer.Reply(std::move(v));
  }
  void GetVectorOfHandleStruct(uint32_t count,
                               GetVectorOfHandleStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<test::wire::HandleStruct> v(allocator, count);
    for (auto& item : v) {
      zx::event::create(0, &item.h);
    }
    completer.Reply(std::move(v));
  }
  void GetVectorOfVectorOfHandleStruct(
      uint32_t count1, uint32_t count2,
      GetVectorOfVectorOfHandleStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<fidl::VectorView<test::wire::HandleStruct>> v(allocator, count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1].Allocate(allocator, count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        zx::event::create(0, &v[i1][i2].h);
      }
    }
    completer.Reply(std::move(v));
  }
  void GetVectorOfVectorOfVectorOfHandleStruct(
      uint32_t count1, uint32_t count2, uint32_t count3,
      GetVectorOfVectorOfVectorOfHandleStructCompleter::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<fidl::VectorView<fidl::VectorView<test::wire::HandleStruct>>> v(allocator,
                                                                                     count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1].Allocate(allocator, count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        v[i1][i2].Allocate(allocator, count3);
        for (uint32_t i3 = 0; i3 < count3; ++i3) {
          zx::event::create(0, &v[i1][i2][i3].h);
        }
      }
    }
    completer.Reply(std::move(v));
  }
  void GetArrayOfHandle(GetArrayOfHandleCompleter::Sync& completer) override {
    fidl::Array<zx::event, 2> a;
    for (auto& item : a) {
      zx::event::create(0, &item);
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfArrayOfHandle(GetArrayOfArrayOfHandleCompleter::Sync& completer) override {
    fidl::Array<fidl::Array<zx::event, 2>, 3> a;
    for (auto& item1 : a) {
      for (auto& item2 : item1) {
        zx::event::create(0, &item2);
      }
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfArrayOfArrayOfHandle(
      GetArrayOfArrayOfArrayOfHandleCompleter::Sync& completer) override {
    fidl::Array<fidl::Array<fidl::Array<zx::event, 2>, 3>, 4> a;
    for (auto& item1 : a) {
      for (auto& item2 : item1) {
        for (auto& item3 : item2) {
          zx::event::create(0, &item3);
        }
      }
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfHandleStruct(GetArrayOfHandleStructCompleter::Sync& completer) override {
    fidl::Array<test::wire::HandleStruct, 2> a;
    for (auto& item : a) {
      zx::event::create(0, &item.h);
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfArrayOfHandleStruct(
      GetArrayOfArrayOfHandleStructCompleter::Sync& completer) override {
    fidl::Array<fidl::Array<test::wire::HandleStruct, 2>, 3> a;
    for (auto& item1 : a) {
      for (auto& item2 : item1) {
        zx::event::create(0, &item2.h);
      }
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfArrayOfArrayOfHandleStruct(
      GetArrayOfArrayOfArrayOfHandleStructCompleter::Sync& completer) override {
    fidl::Array<fidl::Array<fidl::Array<test::wire::HandleStruct, 2>, 3>, 4> a;
    for (auto& item1 : a) {
      for (auto& item2 : item1) {
        for (auto& item3 : item2) {
          zx::event::create(0, &item3.h);
        }
      }
    }
    completer.Reply(std::move(a));
  }
  void GetMixed1(uint32_t count, GetMixed1Completer::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::Array<fidl::VectorView<zx::event>, 2> a;
    for (auto& item1 : a) {
      item1.Allocate(allocator, count);
      for (auto& item2 : item1) {
        zx::event::create(0, &item2);
      }
    }
    completer.Reply(std::move(a));
  }
  void GetMixed2(uint32_t count, GetMixed2Completer::Sync& completer) override {
    fidl::FidlAllocator allocator;
    fidl::VectorView<fidl::Array<zx::event, 2>> v(allocator, count);
    for (auto& item1 : v) {
      for (auto& item2 : item1) {
        zx::event::create(0, &item2);
      }
    }
    completer.Reply(std::move(v));
  }
};

class HandleCloseTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_handle_server"), ZX_OK);

    auto endpoints = fidl::CreateEndpoints<test::HandleProvider>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    client_end_ = std::move(endpoints->client);
    server_ = std::make_unique<HandleCloseProviderServer>();
    fidl::BindServer(loop_->dispatcher(), std::move(endpoints->server), server_.get());
  }

  fidl::WireSyncClient<test::HandleProvider> TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::HandleProvider>(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<HandleCloseProviderServer> server_;
  fidl::ClientEnd<test::HandleProvider> client_end_;
};

TEST_F(HandleCloseTest, Handle) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandle();

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleStructStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleStructStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.s.h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, MultiFieldStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetMultiFieldStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.h1);
    checker.AddEvent(result->value.s.h);
    checker.AddEvent(result->value.h2);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, MultiArgs) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetMultiArgs();

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->h1);
    checker.AddEvent(result->s.h);
    checker.AddEvent(result->h2);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetVectorStruct(4);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.v.count(); ++i) {
      checker.AddEvent(result->value.v[i].h);
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    for (size_t i = 0; i < result->value.a.size(); ++i) {
      checker.AddEvent(result->value.a[i].h);
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleUnion1) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnion(1);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.is_h1());
    checker.AddEvent(result->value.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleUnion2) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnion(2);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.is_h2());
    checker.AddEvent(result->value.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleUnionStruct1) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnionStruct(1);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.u.is_h1());
    checker.AddEvent(result->value.u.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleUnionStruct2) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnionStruct(2);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.u.is_h2());
    checker.AddEvent(result->value.u.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleTableNone) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetHandleTable(0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, HandleTableEvent) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleTable(1);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleTableHandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleTable(2);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleTableAll) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleTable(3);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.h1());
    checker.AddEvent(result->value.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleTableStructNone) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetHandleTableStruct(0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, HandleTableStructEvent) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleTableStruct(1);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.t.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleTableStructHandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleTableStruct(2);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.t.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleTableStructAll) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleTableStruct(3);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.t.h1());
    checker.AddEvent(result->value.t.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleStructNotDefined) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleStruct(false);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, OptionalHandleStructDefined) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleStruct(true);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value->h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleUnionNone) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleUnion(0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, OptionalHandleUnion1) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleUnion(1);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.is_h1());
    checker.AddEvent(result->value.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleUnion2) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleUnion(2);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.is_h2());
    checker.AddEvent(result->value.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleUnionStructNotDefined) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleUnionStruct(false, 0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, OptionalHandleUnionStruct1) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleUnionStruct(true, 1);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value->u.is_h1());
    checker.AddEvent(result->value->u.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleUnionStruct2) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleUnionStruct(true, 2);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value->u.is_h2());
    checker.AddEvent(result->value->u.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleTableStructNotDefined) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleTableStruct(false, 0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, OptionalHandleTableStructNone) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleTableStruct(true, 0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, OptionalHandleTableStructEvent) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleTableStruct(true, 1);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value->t.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleTableStructHandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleTableStruct(true, 2);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value->t.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, OptionalHandleTableStructAll) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetOptionalHandleTableStruct(true, 3);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value->t.h1());
    checker.AddEvent(result->value->t.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleStructOptionalStructNotDefined) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetHandleStructOptionalStruct(false);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, HandleStructOptionalStructDefined) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleStructOptionalStruct(true);

    ASSERT_TRUE(result.ok()) << result.error();

    checker.AddEvent(result->value.s->h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleUnionOptionalStructNotDefined) {
  // Only checks that the destructions won't crash.
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnionOptionalStruct(false, 0);

    ASSERT_TRUE(result.ok()) << result.error();
  }
}

TEST_F(HandleCloseTest, HandleUnionOptionalStruct1) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnionOptionalStruct(true, 1);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.u.is_h1());
    checker.AddEvent(result->value.u.h1());
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, HandleUnionOptionalStruct2) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetHandleUnionOptionalStruct(true, 2);

    ASSERT_TRUE(result.ok()) << result.error();

    ASSERT_TRUE(result->value.u.is_h2());
    checker.AddEvent(result->value.u.h2().h);
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorOfHandle) {
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetVectorOfHandle(kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.count(); ++i) {
      checker.AddEvent(result->value[i]);
    }
  }

  ASSERT_EQ(checker.size(), kNumHandle);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorOfVectorOfHandle) {
  constexpr size_t kNumVector = 4;
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetVectorOfVectorOfHandle(kNumVector, kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.count(); ++i) {
      for (uint32_t j = 0; j < result->value[i].count(); ++j) {
        checker.AddEvent(result->value[i][j]);
      }
    }
  }

  ASSERT_EQ(checker.size(), kNumVector * kNumHandle);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorOfVectorOfVectorOfHandle) {
  constexpr size_t kNumVector1 = 3;
  constexpr size_t kNumVector2 = 4;
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetVectorOfVectorOfVectorOfHandle(kNumVector1, kNumVector2, kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.count(); ++i) {
      for (uint32_t j = 0; j < result->value[i].count(); ++j) {
        for (uint32_t k = 0; k < result->value[i][j].count(); ++k) {
          checker.AddEvent(result->value[i][j][k]);
        }
      }
    }
  }

  ASSERT_EQ(checker.size(), kNumVector1 * kNumVector2 * kNumHandle);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorOfHandleStruct) {
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetVectorOfHandleStruct(kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.count(); ++i) {
      checker.AddEvent(result->value[i].h);
    }
  }

  ASSERT_EQ(checker.size(), kNumHandle);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorOfVectorOfHandleStruct) {
  constexpr size_t kNumVector = 4;
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetVectorOfVectorOfHandleStruct(kNumVector, kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.count(); ++i) {
      for (uint32_t j = 0; j < result->value[i].count(); ++j) {
        checker.AddEvent(result->value[i][j].h);
      }
    }
  }

  ASSERT_EQ(checker.size(), kNumVector * kNumHandle);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, VectorOfVectorOfVectorOfHandleStruct) {
  constexpr size_t kNumVector1 = 3;
  constexpr size_t kNumVector2 = 4;
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result =
        client.GetVectorOfVectorOfVectorOfHandleStruct(kNumVector1, kNumVector2, kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (uint32_t i = 0; i < result->value.count(); ++i) {
      for (uint32_t j = 0; j < result->value[i].count(); ++j) {
        for (uint32_t k = 0; k < result->value[i][j].count(); ++k) {
          checker.AddEvent(result->value[i][j][k].h);
        }
      }
    }
  }

  ASSERT_EQ(checker.size(), kNumVector1 * kNumVector2 * kNumHandle);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayOfHandle) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayOfHandle();

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item : result->value) {
      checker.AddEvent(item);
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayOfArrayOfHandle) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayOfArrayOfHandle();

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item1 : result->value) {
      for (const auto& item2 : item1) {
        checker.AddEvent(item2);
      }
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayOfArrayOfArrayOfHandle) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayOfArrayOfArrayOfHandle();

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item1 : result->value) {
      for (const auto& item2 : item1) {
        for (const auto& item3 : item2) {
          checker.AddEvent(item3);
        }
      }
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayOfHandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayOfHandleStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item : result->value) {
      checker.AddEvent(item.h);
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayOfArrayOfHandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayOfArrayOfHandleStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item1 : result->value) {
      for (const auto& item2 : item1) {
        checker.AddEvent(item2.h);
      }
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, ArrayOfArrayOfArrayOfHandleStruct) {
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetArrayOfArrayOfArrayOfHandleStruct();

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item1 : result->value) {
      for (const auto& item2 : item1) {
        for (const auto& item3 : item2) {
          checker.AddEvent(item3.h);
        }
      }
    }
  }

  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, Mixed1) {
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetMixed1(kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item1 : result->value) {
      for (const auto& item2 : item1) {
        checker.AddEvent(item2);
      }
    }
  }

  ASSERT_EQ(checker.size(), kNumHandle * 2);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}

TEST_F(HandleCloseTest, Mixed2) {
  constexpr size_t kNumHandle = 5;
  llcpp_types_test_utils::HandleChecker checker;
  auto client = TakeClient();
  {
    auto result = client.GetMixed2(kNumHandle);

    ASSERT_TRUE(result.ok()) << result.error();

    for (auto& item1 : result->value) {
      for (const auto& item2 : item1) {
        checker.AddEvent(item2);
      }
    }
  }

  ASSERT_EQ(checker.size(), kNumHandle * 2);
  // After the destruction of the result, each handle in dupes should have only one link.
  checker.CheckEvents();
}
