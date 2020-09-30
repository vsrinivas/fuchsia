// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/object.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <cstdint>

#include <gtest/gtest.h>
#include <llcpptest/handles/test/llcpp/fidl.h>

namespace test = ::llcpp::llcpptest::handles::test;

// All the tests in this file check that when a result is freed, all the handles inside the result
// are closed.

namespace {

class HandleChecker {
 public:
  HandleChecker() = default;

  size_t size() const { return events_.size(); }

  void AddEvent(const zx::event& event) {
    ASSERT_TRUE(event.is_valid());
    zx::event new_event;
    ASSERT_EQ(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &new_event), ZX_OK);
    events_.emplace_back(std::move(new_event));
  }

  void CheckEvents() {
    for (size_t i = 0; i < events_.size(); ++i) {
      zx_info_handle_count_t info = {};
      auto status =
          events_[i].get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
      ZX_ASSERT(status == ZX_OK);
      EXPECT_EQ(info.handle_count, 1U) << "Handle not freed " << (i + 1) << '/' << events_.size();
    }
  }

 private:
  std::vector<zx::event> events_;
};

}  // namespace

class HandleCloseProviderServer : public test::HandleProvider::Interface {
 public:
  void GetHandle(GetHandleCompleter::Sync& completer) override {
    zx::event e;
    zx::event::create(0, &e);
    completer.Reply(std::move(e));
  }
  void GetHandleStruct(GetHandleStructCompleter::Sync& completer) override {
    test::HandleStruct s;
    zx::event::create(0, &s.h);
    completer.Reply(std::move(s));
  }
  void GetHandleStructStruct(GetHandleStructStructCompleter::Sync& completer) override {
    test::HandleStructStruct s;
    zx::event::create(0, &s.s.h);
    completer.Reply(std::move(s));
  }
  void GetMultiFieldStruct(GetMultiFieldStructCompleter::Sync& completer) override {
    test::MultiFieldStruct s;
    zx::event::create(0, &s.h1);
    zx::event::create(0, &s.s.h);
    zx::event::create(0, &s.h2);
    completer.Reply(std::move(s));
  }
  void GetMultiArgs(GetMultiArgsCompleter::Sync& completer) override {
    zx::event h1;
    zx::event::create(0, &h1);
    test::HandleStruct s;
    zx::event::create(0, &s.h);
    zx::event h2;
    zx::event::create(0, &h2);
    completer.Reply(std::move(h1), std::move(s), std::move(h2));
  }
  void GetVectorStruct(uint32_t count, GetVectorStructCompleter::Sync& completer) override {
    std::vector<test::HandleStruct> v(count);
    for (auto& s : v) {
      zx::event::create(0, &s.h);
    }
    test::VectorStruct s;
    s.v = fidl::unowned_vec(v);
    completer.Reply(std::move(s));
  }
  void GetArrayStruct(GetArrayStructCompleter::Sync& completer) override {
    test::ArrayStruct s;
    for (size_t i = 0; i < s.a.size(); ++i) {
      zx::event::create(0, &s.a[i].h);
    }
    completer.Reply(std::move(s));
  }
  void GetHandleUnion(int32_t field, GetHandleUnionCompleter::Sync& completer) override {
    test::HandleUnion u;
    test::HandleStruct s;
    zx::event e;
    zx::event::create(0, &e);
    if (field == 1) {
      u = test::HandleUnion::WithH1(fidl::unowned_ptr(&e));
    } else if (field == 2) {
      s.h = std::move(e);
      u = test::HandleUnion::WithH2(fidl::unowned_ptr(&s));
    }
    completer.Reply(std::move(u));
  }
  void GetHandleUnionStruct(int32_t field,
                            GetHandleUnionStructCompleter::Sync& completer) override {
    test::HandleUnionStruct u;
    test::HandleStruct s;
    zx::event e;
    zx::event::create(0, &e);
    if (field == 1) {
      u.u = test::HandleUnion::WithH1(fidl::unowned_ptr(&e));
    } else if (field == 2) {
      s.h = std::move(e);
      u.u = test::HandleUnion::WithH2(fidl::unowned_ptr(&s));
    }
    completer.Reply(std::move(u));
  }
  void GetHandleTable(uint32_t fields, GetHandleTableCompleter::Sync& completer) override {
    zx::event e;
    test::HandleStruct s;
    test::HandleTable::Builder builder(std::make_unique<test::HandleTable::Frame>());
    if ((fields & 1) != 0) {
      zx::event::create(0, &e);
      builder.set_h1(std::make_unique<zx::event>(std::move(e)));
    }
    if ((fields & 2) != 0) {
      zx::event::create(0, &s.h);
      builder.set_h2(std::make_unique<test::HandleStruct>(std::move(s)));
    }
    test::HandleTable t = builder.build();
    completer.Reply(std::move(t));
  }
  void GetHandleTableStruct(uint32_t fields,
                            GetHandleTableStructCompleter::Sync& completer) override {
    zx::event e;
    test::HandleStruct s;
    test::HandleTable::Builder builder(std::make_unique<test::HandleTable::Frame>());
    if ((fields & 1) != 0) {
      zx::event::create(0, &e);
      builder.set_h1(std::make_unique<zx::event>(std::move(e)));
    }
    if ((fields & 2) != 0) {
      zx::event::create(0, &s.h);
      builder.set_h2(std::make_unique<test::HandleStruct>(std::move(s)));
    }
    test::HandleTableStruct reply;
    reply.t = builder.build();
    completer.Reply(std::move(reply));
  }
  void GetOptionalHandleStruct(bool defined,
                               GetOptionalHandleStructCompleter::Sync& completer) override {
    if (defined) {
      test::HandleStruct s;
      zx::event::create(0, &s.h);
      completer.Reply(fidl::unowned_ptr(&s));
    } else {
      completer.Reply(nullptr);
    }
  }
  void GetOptionalHandleUnion(int32_t field,
                              GetOptionalHandleUnionCompleter::Sync& completer) override {
    test::HandleUnion u;
    test::HandleStruct s;
    zx::event e;
    zx::event::create(0, &e);
    if (field == 1) {
      u = test::HandleUnion::WithH1(fidl::unowned_ptr(&e));
    } else if (field == 2) {
      s.h = std::move(e);
      u = test::HandleUnion::WithH2(fidl::unowned_ptr(&s));
    }
    completer.Reply(std::move(u));
  }
  void GetOptionalHandleUnionStruct(
      bool defined, int32_t field,
      GetOptionalHandleUnionStructCompleter::Sync& completer) override {
    if (defined) {
      test::HandleUnionStruct u;
      test::HandleStruct s;
      zx::event e;
      zx::event::create(0, &e);
      if (field == 1) {
        u.u = test::HandleUnion::WithH1(fidl::unowned_ptr(&e));
      } else if (field == 2) {
        s.h = std::move(e);
        u.u = test::HandleUnion::WithH2(fidl::unowned_ptr(&s));
      }
      completer.Reply(fidl::unowned_ptr(&u));
    } else {
      completer.Reply(nullptr);
    }
  }
  void GetOptionalHandleTableStruct(
      bool defined, uint32_t fields,
      GetOptionalHandleTableStructCompleter::Sync& completer) override {
    if (defined) {
      zx::event e;
      test::HandleStruct s;
      test::HandleTable::Builder builder(std::make_unique<test::HandleTable::Frame>());
      if ((fields & 1) != 0) {
        zx::event::create(0, &e);
        builder.set_h1(std::make_unique<zx::event>(std::move(e)));
      }
      if ((fields & 2) != 0) {
        zx::event::create(0, &s.h);
        builder.set_h2(std::make_unique<test::HandleStruct>(std::move(s)));
      }
      test::HandleTableStruct reply;
      reply.t = builder.build();
      completer.Reply(fidl::unowned_ptr(&reply));
    } else {
      completer.Reply(nullptr);
    }
  }
  void GetHandleStructOptionalStruct(
      bool defined, GetHandleStructOptionalStructCompleter::Sync& completer) override {
    test::HandleStructOptionalStruct reply;
    test::HandleStruct s;
    if (defined) {
      zx::event::create(0, &s.h);
      reply.s = fidl::unowned_ptr(&s);
    }
    completer.Reply(std::move(reply));
  }
  void GetHandleUnionOptionalStruct(
      bool defined, int32_t field,
      GetHandleUnionOptionalStructCompleter::Sync& completer) override {
    test::HandleUnionOptionalStruct reply;
    test::HandleUnion u;
    test::HandleStruct s;
    zx::event e;
    if (defined) {
      zx::event::create(0, &e);
      if (field == 1) {
        u = test::HandleUnion::WithH1(fidl::unowned_ptr(&e));
      } else if (field == 2) {
        s.h = std::move(e);
        u = test::HandleUnion::WithH2(fidl::unowned_ptr(&s));
      }
      reply.u = std::move(u);
    }
    completer.Reply(std::move(reply));
  }
  void GetVectorOfHandle(uint32_t count, GetVectorOfHandleCompleter::Sync& completer) override {
    std::vector<zx::event> v(count);
    for (auto& item : v) {
      zx::event::create(0, &item);
    }
    completer.Reply(fidl::unowned_vec(v));
  }
  void GetVectorOfVectorOfHandle(uint32_t count1, uint32_t count2,
                                 GetVectorOfVectorOfHandleCompleter::Sync& completer) override {
    std::vector<fidl::VectorView<zx::event>> v(count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1] = fidl::VectorView(
          fidl::tracking_ptr<zx::event[]>(std::make_unique<zx::event[]>(count2)), count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        zx::event::create(0, &v[i1][i2]);
      }
    }
    completer.Reply(fidl::unowned_vec(v));
  }
  void GetVectorOfVectorOfVectorOfHandle(
      uint32_t count1, uint32_t count2, uint32_t count3,
      GetVectorOfVectorOfVectorOfHandleCompleter::Sync& completer) override {
    std::vector<fidl::VectorView<fidl::VectorView<zx::event>>> v(count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1] = fidl::VectorView(fidl::tracking_ptr<fidl::VectorView<zx::event>[]>(
                                   std::make_unique<fidl::VectorView<zx::event>[]>(count2)),
                               count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        v[i1][i2] = fidl::VectorView(
            fidl::tracking_ptr<zx::event[]>(std::make_unique<zx::event[]>(count3)), count3);
        for (uint32_t i3 = 0; i3 < count3; ++i3) {
          zx::event::create(0, &v[i1][i2][i3]);
        }
      }
    }
    completer.Reply(fidl::unowned_vec(v));
  }
  void GetVectorOfHandleStruct(uint32_t count,
                               GetVectorOfHandleStructCompleter::Sync& completer) override {
    std::vector<test::HandleStruct> v(count);
    for (auto& item : v) {
      zx::event::create(0, &item.h);
    }
    completer.Reply(fidl::unowned_vec(v));
  }
  void GetVectorOfVectorOfHandleStruct(
      uint32_t count1, uint32_t count2,
      GetVectorOfVectorOfHandleStructCompleter::Sync& completer) override {
    std::vector<fidl::VectorView<test::HandleStruct>> v(count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1] = fidl::VectorView(
          fidl::tracking_ptr<test::HandleStruct[]>(std::make_unique<test::HandleStruct[]>(count2)),
          count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        zx::event::create(0, &v[i1][i2].h);
      }
    }
    completer.Reply(fidl::unowned_vec(v));
  }
  void GetVectorOfVectorOfVectorOfHandleStruct(
      uint32_t count1, uint32_t count2, uint32_t count3,
      GetVectorOfVectorOfVectorOfHandleStructCompleter::Sync& completer) override {
    std::vector<fidl::VectorView<fidl::VectorView<test::HandleStruct>>> v(count1);
    for (uint32_t i1 = 0; i1 < count1; ++i1) {
      v[i1] =
          fidl::VectorView(fidl::tracking_ptr<fidl::VectorView<test::HandleStruct>[]>(
                               std::make_unique<fidl::VectorView<test::HandleStruct>[]>(count2)),
                           count2);
      for (uint32_t i2 = 0; i2 < count2; ++i2) {
        v[i1][i2] = fidl::VectorView(fidl::tracking_ptr<test::HandleStruct[]>(
                                         std::make_unique<test::HandleStruct[]>(count3)),
                                     count3);
        for (uint32_t i3 = 0; i3 < count3; ++i3) {
          zx::event::create(0, &v[i1][i2][i3].h);
        }
      }
    }
    completer.Reply(fidl::unowned_vec(v));
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
    fidl::Array<test::HandleStruct, 2> a;
    for (auto& item : a) {
      zx::event::create(0, &item.h);
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfArrayOfHandleStruct(
      GetArrayOfArrayOfHandleStructCompleter::Sync& completer) override {
    fidl::Array<fidl::Array<test::HandleStruct, 2>, 3> a;
    for (auto& item1 : a) {
      for (auto& item2 : item1) {
        zx::event::create(0, &item2.h);
      }
    }
    completer.Reply(std::move(a));
  }
  void GetArrayOfArrayOfArrayOfHandleStruct(
      GetArrayOfArrayOfArrayOfHandleStructCompleter::Sync& completer) override {
    fidl::Array<fidl::Array<fidl::Array<test::HandleStruct, 2>, 3>, 4> a;
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
    fidl::Array<fidl::VectorView<zx::event>, 2> a;
    for (auto& item1 : a) {
      item1 = fidl::VectorView(
          fidl::tracking_ptr<zx::event[]>(std::make_unique<zx::event[]>(count)), count);
      for (auto& item2 : item1) {
        zx::event::create(0, &item2);
      }
    }
    completer.Reply(std::move(a));
  }
  void GetMixed2(uint32_t count, GetMixed2Completer::Sync& completer) override {
    std::vector<fidl::Array<zx::event, 2>> v(count);
    for (auto& item1 : v) {
      for (auto& item2 : item1) {
        zx::event::create(0, &item2);
      }
    }
    completer.Reply(fidl::unowned_vec(v));
  }
};

class HandleCloseTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_handle_server"), ZX_OK);

    zx::channel server_end;
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end), ZX_OK);
    server_ = std::make_unique<HandleCloseProviderServer>();
    fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end), server_.get());
  }

  test::HandleProvider::SyncClient TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::HandleProvider::SyncClient(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<HandleCloseProviderServer> server_;
  zx::channel client_end_;
};

TEST_F(HandleCloseTest, Handle) {
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
  HandleChecker checker;
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
