// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/internal/client_continuation.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

using fidl::internal::WeakCallbackFactory;

auto GetFakeClient() { return std::make_shared<fidl::internal::ClientControlBlock>(nullptr); }

// Example user object acting as callback receivers.
struct Receiver {
  // Using |int| as our result type in tests.
  // In production it would be a proper result type such as |fit::result|.
  void Speak(int answer) const { *out_answer = answer; }
  int* out_answer;
};

constexpr int kCanceledAnswer = 0;
constexpr int kSuccessAnswer = 42;

TEST(ClientContinuation, PassivateCallback) {
  // Client is alive -> called.
  {
    std::shared_ptr fake_client = GetFakeClient();
    int answer = kCanceledAnswer;
    Receiver receiver{&answer};
    auto cb = WeakCallbackFactory<int>{fake_client}.Then(
        [&receiver](int& answer) { receiver.Speak(answer); });
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
        [&receiver](int& answer) { receiver.Speak(answer); });
    int result = kSuccessAnswer;

    fake_client.reset();
    cb.Run(result);

    EXPECT_EQ(kCanceledAnswer, answer);
  }
}

TEST(ClientContinuation, SupportGenericLambda) {
  std::shared_ptr fake_client = GetFakeClient();
  int answer = kCanceledAnswer;
  std::unique_ptr receiver = std::make_unique<Receiver>(Receiver{&answer});
  auto cb = WeakCallbackFactory<int>{fake_client}.Then(
      [receiver = std::move(receiver)](auto&& answer) { receiver->Speak(answer); });
  int result = kSuccessAnswer;

  cb.Run(result);

  EXPECT_EQ(kSuccessAnswer, answer);
}

}  // namespace
