// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/tests/util/test_utils.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/compiler/interfaces/tests/sample_import.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/sample_interfaces.fidl.h"

namespace fidl {
namespace test {
namespace {

class ProviderImpl : public sample::Provider {
 public:
  explicit ProviderImpl(InterfaceRequest<sample::Provider> request)
      : binding_(this, std::move(request)) {}

  void EchoString(const String& a,
                  const std::function<void(String)>& callback) override {
    std::function<void(String)> callback_copy;
    // Make sure operator= is used.
    callback_copy = callback;
    callback_copy(a);
  }

  void EchoStrings(const String& a,
                   const String& b,
                   const std::function<void(String, String)>& callback) override {
    callback(a, b);
  }

  void EchoMessagePipeHandle(
      zx::channel a,
      const std::function<void(zx::channel)>& callback) override {
    callback(std::move(a));
  }

  void EchoEnum(sample::Enum a,
                const std::function<void(sample::Enum)>& callback) override {
    callback(a);
  }

  void EchoInt(int32_t a, const EchoIntCallback& callback) override {
    callback(a);
  }

  Binding<sample::Provider> binding_;
};

class StringRecorder {
 public:
  explicit StringRecorder(std::string* buf) : buf_(buf) {}
  void operator()(const String& a) const { *buf_ = a; }
  void operator()(const String& a, const String& b) const {
    *buf_ = a.get() + b.get();
  }

 private:
  std::string* buf_;
};

class EnumRecorder {
 public:
  explicit EnumRecorder(sample::Enum* value) : value_(value) {}
  void operator()(sample::Enum a) const { *value_ = a; }

 private:
  sample::Enum* value_;
};

class MessagePipeWriter {
 public:
  explicit MessagePipeWriter(const char* text) : text_(text) {}
  void operator()(zx::channel handle) const { WriteTextMessage(handle, text_); }

 private:
  std::string text_;
};

class RequestResponseTest : public testing::Test {
 public:
  ~RequestResponseTest() override {}
  void TearDown() override { ClearAsyncWaiter(); }
  void PumpMessages() { WaitForAsyncWaiter(); }
};

TEST_F(RequestResponseTest, EchoString) {
  sample::ProviderPtr provider;
  ProviderImpl provider_impl(provider.NewRequest());

  std::string buf;
  provider->EchoString(String::From("hello"), StringRecorder(&buf));

  PumpMessages();

  EXPECT_EQ(std::string("hello"), buf);
}

TEST_F(RequestResponseTest, EchoStrings) {
  sample::ProviderPtr provider;
  ProviderImpl provider_impl(provider.NewRequest());

  std::string buf;
  provider->EchoStrings(String::From("hello"), String::From(" world"),
                        StringRecorder(&buf));

  PumpMessages();

  EXPECT_EQ(std::string("hello world"), buf);
}

TEST_F(RequestResponseTest, EchoMessagePipeHandle) {
  sample::ProviderPtr provider;
  ProviderImpl provider_impl(provider.NewRequest());

  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  provider->EchoMessagePipeHandle(std::move(handle1),
                                  MessagePipeWriter("hello"));

  PumpMessages();

  std::string value;
  ReadTextMessage(handle0, &value);

  EXPECT_EQ(std::string("hello"), value);
}

TEST_F(RequestResponseTest, EchoEnum) {
  sample::ProviderPtr provider;
  ProviderImpl provider_impl(provider.NewRequest());

  sample::Enum value;
  provider->EchoEnum(sample::Enum::VALUE, EnumRecorder(&value));

  PumpMessages();

  EXPECT_EQ(sample::Enum::VALUE, value);
}

}  // namespace
}  // namespace test
}  // namespace fidl
