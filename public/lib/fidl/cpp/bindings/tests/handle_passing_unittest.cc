// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <utility>

#include <mx/datapipe.h>
#include <magenta/syscalls/datapipe.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"
#include "lib/fidl/cpp/bindings/tests/util/test_utils.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/compiler/interfaces/tests/sample_factory.fidl.h"

namespace fidl {
namespace test {
namespace {

const char kText1[] = "hello";
const char kText2[] = "world";

class StringRecorder {
 public:
  explicit StringRecorder(std::string* buf) : buf_(buf) {}
  void operator()(const String& a) const { *buf_ = a.To<std::string>(); }

 private:
  std::string* buf_;
};

class ImportedInterfaceImpl : public imported::ImportedInterface {
 public:
  explicit ImportedInterfaceImpl(
      InterfaceRequest<imported::ImportedInterface> request)
      : binding_(this, std::move(request)) {}

  void DoSomething() override { do_something_count_++; }

  static int do_something_count() { return do_something_count_; }

 private:
  static int do_something_count_;
  Binding<ImportedInterface> binding_;
};
int ImportedInterfaceImpl::do_something_count_ = 0;

class SampleNamedObjectImpl : public sample::NamedObject {
 public:
  explicit SampleNamedObjectImpl(InterfaceRequest<sample::NamedObject> request)
      : binding_(this, std::move(request)) {
    binding_.set_connection_error_handler([this](){ delete this; });
  }
  void SetName(const fidl::String& name) override { name_ = name; }

  void GetName(const std::function<void(fidl::String)>& callback) override {
    callback(name_);
  }

 private:
  std::string name_;
  Binding<sample::NamedObject> binding_;
};

class SampleFactoryImpl : public sample::Factory {
 public:
  explicit SampleFactoryImpl(InterfaceRequest<sample::Factory> request)
      : binding_(this, std::move(request)) {}

  void DoStuff(sample::RequestPtr request,
               mx::channel pipe,
               const DoStuffCallback& callback) override {
    std::string text1;
    if (pipe)
      EXPECT_TRUE(ReadTextMessage(pipe, &text1));

    std::string text2;
    if (request->pipe) {
      EXPECT_TRUE(ReadTextMessage(request->pipe, &text2));

      // Ensure that simply accessing request->pipe does not close it.
      EXPECT_TRUE(request->pipe);
    }

    mx::channel pipe0;
    if (!text2.empty()) {
      mx::channel::create(0, &pipe0, &pipe1_);
      EXPECT_TRUE(WriteTextMessage(pipe1_, text2));
    }

    sample::ResponsePtr response(sample::Response::New());
    response->x = 2;
    response->pipe = std::move(pipe0);
    callback(std::move(response), text1);

    if (request->obj)
      imported::ImportedInterfacePtr::Create(std::move(request->obj))
          ->DoSomething();
  }

  void DoStuff2(mx::datapipe_consumer pipe,
                const DoStuff2Callback& callback) override {
    // Read the data from the pipe, writing the response (as a string) to
    // DidStuff2().
    ASSERT_TRUE(pipe);
    mx_size_t data_size = 0u;
    ASSERT_EQ(NO_ERROR, pipe.read(MX_DATAPIPE_READ_FLAG_QUERY, nullptr, 0,
              &data_size));
    ASSERT_NE(0, static_cast<int>(data_size));
    char data[64];
    ASSERT_LT(static_cast<int>(data_size), 64);
    ASSERT_EQ(NO_ERROR, pipe.read(MX_DATAPIPE_READ_FLAG_ALL_OR_NONE, data,
              data_size, &data_size));

    callback(data);
  }

  void CreateNamedObject(
      InterfaceRequest<sample::NamedObject> object_request) override {
    EXPECT_TRUE(object_request.is_pending());
    new SampleNamedObjectImpl(std::move(object_request));
  }

  // These aren't called or implemented, but exist here to test that the
  // methods are generated with the correct argument types for imported
  // interfaces.
  void RequestImportedInterface(
      InterfaceRequest<imported::ImportedInterface> imported,
      const std::function<void(InterfaceRequest<imported::ImportedInterface>)>&
          callback) override {}
  void TakeImportedInterface(
      InterfaceHandle<imported::ImportedInterface> imported,
      const std::function<void(InterfaceHandle<imported::ImportedInterface>)>&
          callback) override {}

 private:
  mx::channel pipe1_;
  Binding<sample::Factory> binding_;
};

class HandlePassingTest : public testing::Test {
 public:
  void TearDown() override { ClearAsyncWaiter(); }

  void PumpMessages() { WaitForAsyncWaiter(); }
};

struct DoStuffCallback {
  DoStuffCallback(bool* got_response, std::string* got_text_reply)
      : got_response(got_response), got_text_reply(got_text_reply) {}

  void operator()(sample::ResponsePtr response, const String& text_reply) const {
    *got_text_reply = text_reply;

    if (response->pipe) {
      std::string text2;
      EXPECT_TRUE(ReadTextMessage(response->pipe, &text2));

      // Ensure that simply accessing response.pipe does not close it.
      EXPECT_TRUE(response->pipe);

      EXPECT_EQ(std::string(kText2), text2);

      // Do some more tests of handle passing:
      mx::channel p = std::move(response->pipe);
      EXPECT_TRUE(p);
      EXPECT_FALSE(response->pipe);
    }

    *got_response = true;
  }

  bool* got_response;
  std::string* got_text_reply;
};

TEST_F(HandlePassingTest, Basic) {
  sample::FactoryPtr factory;
  SampleFactoryImpl factory_impl(factory.NewRequest());

  mx::channel pipe0_handle0, pipe0_handle1;
  mx::channel::create(0, &pipe0_handle0, &pipe0_handle1);
  EXPECT_TRUE(WriteTextMessage(pipe0_handle1, kText1));

  mx::channel pipe1_handle0, pipe1_handle1;
  mx::channel::create(0, &pipe1_handle0, &pipe1_handle1);
  EXPECT_TRUE(WriteTextMessage(pipe1_handle1, kText2));

  imported::ImportedInterfacePtr imported;
  ImportedInterfaceImpl imported_impl(imported.NewRequest());

  sample::RequestPtr request(sample::Request::New());
  request->x = 1;
  request->pipe = std::move(pipe1_handle0);
  request->obj = std::move(imported);
  bool got_response = false;
  std::string got_text_reply;
  DoStuffCallback cb(&got_response, &got_text_reply);
  factory->DoStuff(std::move(request), std::move(pipe0_handle0), cb);

  EXPECT_FALSE(*cb.got_response);
  int count_before = ImportedInterfaceImpl::do_something_count();

  PumpMessages();

  EXPECT_TRUE(*cb.got_response);
  EXPECT_EQ(kText1, *cb.got_text_reply);
  EXPECT_EQ(1, ImportedInterfaceImpl::do_something_count() - count_before);
}

TEST_F(HandlePassingTest, PassInvalid) {
  sample::FactoryPtr factory;
  SampleFactoryImpl factory_impl(factory.NewRequest());

  sample::RequestPtr request(sample::Request::New());
  request->x = 1;
  bool got_response = false;
  std::string got_text_reply;
  DoStuffCallback cb(&got_response, &got_text_reply);
  factory->DoStuff(std::move(request), mx::channel(), cb);

  EXPECT_FALSE(*cb.got_response);

  PumpMessages();

  EXPECT_TRUE(*cb.got_response);
}

struct DoStuff2Callback {
  DoStuff2Callback(bool* got_response, std::string* got_text_reply)
      : got_response(got_response), got_text_reply(got_text_reply) {}

  void operator()(const String& text_reply) const {
    *got_response = true;
    *got_text_reply = text_reply;
  }

  bool* got_response;
  std::string* got_text_reply;
};

// Verifies DataPipeConsumer can be passed and read from.
TEST_F(HandlePassingTest, DataPipe) {
  sample::FactoryPtr factory;
  SampleFactoryImpl factory_impl(factory.NewRequest());

  // Writes a string to a data pipe and passes the data pipe (consumer) to the
  // factory.
  mx::datapipe_producer producer_handle;
  mx::datapipe_consumer consumer_handle;
  ASSERT_EQ(NO_ERROR, mx::datapipe<void>::create(1, 1024, 0, &producer_handle,
                                                 &consumer_handle));
  std::string expected_text_reply = "got it";
  // +1 for \0.
  mx_size_t data_size = static_cast<mx_size_t>(expected_text_reply.size() + 1);
  ASSERT_EQ(NO_ERROR,
            producer_handle.write(MX_DATAPIPE_READ_FLAG_ALL_OR_NONE,
                expected_text_reply.c_str(), data_size, &data_size));

  bool got_response = false;
  std::string got_text_reply;
  DoStuff2Callback cb(&got_response, &got_text_reply);
  factory->DoStuff2(std::move(consumer_handle), cb);

  EXPECT_FALSE(*cb.got_response);

  PumpMessages();

  EXPECT_TRUE(*cb.got_response);
  EXPECT_EQ(expected_text_reply, *cb.got_text_reply);
}

TEST_F(HandlePassingTest, PipesAreClosed) {
  sample::FactoryPtr factory;
  SampleFactoryImpl factory_impl(factory.NewRequest());

  mx::channel handle0, handle1;
  mx::channel::create(0, &handle0, &handle1);

  mx_handle_t handle0_value = handle0.get();
  mx_handle_t handle1_value = handle1.get();

  {
    auto pipes = Array<mx::channel>::New(2);
    pipes[0] = std::move(handle0);
    pipes[1] = std::move(handle1);

    sample::RequestPtr request(sample::Request::New());
    request->more_pipes = std::move(pipes);

    factory->DoStuff(std::move(request), mx::channel(),
                     [](sample::ResponsePtr, const String&){});
  }

  // We expect the pipes to have been closed.
  EXPECT_EQ(ERR_BAD_HANDLE, mx_handle_close(handle0_value));
  EXPECT_EQ(ERR_BAD_HANDLE, mx_handle_close(handle1_value));
}

TEST_F(HandlePassingTest, CreateNamedObject) {
  sample::FactoryPtr factory;
  SampleFactoryImpl factory_impl(factory.NewRequest());

  sample::NamedObjectPtr object1;
  EXPECT_FALSE(object1);

  InterfaceRequest<sample::NamedObject> object1_request = object1.NewRequest();
  EXPECT_TRUE(object1_request.is_pending());
  factory->CreateNamedObject(std::move(object1_request));
  EXPECT_FALSE(object1_request.is_pending());  // We've passed the request.

  ASSERT_TRUE(object1);
  object1->SetName("object1");

  sample::NamedObjectPtr object2;
  factory->CreateNamedObject(object2.NewRequest());
  object2->SetName("object2");

  std::string name1;
  object1->GetName(StringRecorder(&name1));

  std::string name2;
  object2->GetName(StringRecorder(&name2));

  PumpMessages();  // Yield for results.

  EXPECT_EQ(std::string("object1"), name1);
  EXPECT_EQ(std::string("object2"), name2);
}

}  // namespace
}  // namespace test
}  // namespace fidl
