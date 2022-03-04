// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/internal/client_continuation.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

using fidl::internal::WeakCallbackFactory;

auto GetFakeClient() { return std::make_shared<fidl::internal::ClientControlBlock>(nullptr); }

// Example user object acting as callback receivers.
struct Receiver {
  // Using |int| as our result type in tests.
  // In production it would be a proper result type such as |fitx::result|.
  void Speak(int answer) const { *out_answer = answer; }
  int* out_answer;
};

constexpr int kCanceledAnswer = 0;
constexpr int kSuccessAnswer = 42;

TEST(ClientContinuation, MemberFnActiveWeakPtr) {
  // Client is alive -> called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    cb.Run(result);

    EXPECT_EQ(kSuccessAnswer, answer);
  }

  // Client is destroyed -> still called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    fake_client.reset();
    cb.Run(result);

    EXPECT_EQ(kSuccessAnswer, answer);
  }
}

TEST(ClientContinuation, MemberFnExpiredWeakPtr) {
  // Client is alive -> canceled.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    receiver.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }

  // Client is destroyed -> still canceled.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    fake_client.reset();
    receiver.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }
}

TEST(ClientContinuation, LambdaActiveWeakPtr) {
  // Client is alive -> called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [](Receiver* receiver, int& answer) { receiver->Speak(answer); }, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    cb.Run(result);

    EXPECT_EQ(kSuccessAnswer, answer);
  }

  // Client is destroyed -> still called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [](Receiver* receiver, int& answer) { receiver->Speak(answer); }, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    fake_client.reset();
    cb.Run(result);

    EXPECT_EQ(kSuccessAnswer, answer);
  }
}

TEST(ClientContinuation, LambdaExpiredWeakPtr) {
  // Client is alive -> canceled.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [](Receiver* receiver, int& answer) { receiver->Speak(answer); }, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    receiver.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }

  // Client is destroyed -> still canceled.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [](Receiver* receiver, int& answer) { receiver->Speak(answer); }, std::weak_ptr(receiver));
    int result = kSuccessAnswer;

    fake_client.reset();
    receiver.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }
}

TEST(ClientContinuation, SharedPtr) {
  std::shared_ptr fake_client = GetFakeClient();
  int answer = kCanceledAnswer;
  std::shared_ptr receiver = std::make_shared<Receiver>(Receiver{&answer});
  auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, std::shared_ptr(receiver));
  int result = kSuccessAnswer;

  receiver.reset();
  cb.Run(result);

  EXPECT_EQ(kSuccessAnswer, answer);
}

TEST(ClientContinuation, UniquePtr) {
  std::shared_ptr fake_client = GetFakeClient();
  int answer = kCanceledAnswer;
  std::unique_ptr receiver = std::make_unique<Receiver>(Receiver{&answer});
  auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, std::move(receiver));
  int result = kSuccessAnswer;

  cb.Run(result);

  EXPECT_NULL(receiver);
  EXPECT_EQ(kSuccessAnswer, answer);
}

TEST(ClientContinuation, MemberFnRawPtr) {
  // Client is alive -> called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    Receiver receiver{&answer};
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, &receiver);
    int result = kSuccessAnswer;

    cb.Run(result);

    EXPECT_EQ(kSuccessAnswer, answer);
  }

  // Client is destroyed -> not called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    Receiver receiver{&answer};
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(&Receiver::Speak, &receiver);
    int result = kSuccessAnswer;

    fake_client.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }
}

TEST(ClientContinuation, LambdaRawPtr) {
  // Client is alive -> called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    Receiver receiver{&answer};
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [](Receiver* self, int& answer) { self->Speak(answer); }, &receiver);
    int result = kSuccessAnswer;

    cb.Run(result);

    EXPECT_EQ(kSuccessAnswer, answer);
  }

  // Client is destroyed -> not called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    Receiver receiver{&answer};
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [](Receiver* self, int& answer) { self->Speak(answer); }, &receiver);
    int result = kSuccessAnswer;

    fake_client.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }
}

TEST(ClientContinuation, CurryArguments) {
  std::shared_ptr fake_client = GetFakeClient();
  int answer = kCanceledAnswer;
  std::unique_ptr receiver = std::make_unique<Receiver>(Receiver{&answer});
  auto cb = WeakCallbackFactory<int>{fake_client}.Then(
      [](Receiver* receiver, const std::string& arg, int& answer) {
        EXPECT_EQ("hello", arg);
        receiver->Speak(answer);
      },
      std::move(receiver), "hello");
  int result = kSuccessAnswer;
  cb.Run(result);
  EXPECT_EQ(kSuccessAnswer, answer);
}

TEST(ClientContinuation, GenericLambda) {
  std::shared_ptr fake_client = GetFakeClient();
  int answer = kCanceledAnswer;
  std::unique_ptr receiver = std::make_unique<Receiver>(Receiver{&answer});
  auto cb = WeakCallbackFactory<int>{fake_client}.Then(
      [](Receiver* receiver, auto&& answer) { receiver->Speak(answer); }, std::move(receiver));
  int result = kSuccessAnswer;

  cb.Run(result);

  EXPECT_EQ(kSuccessAnswer, answer);
}

}  // namespace
