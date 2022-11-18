// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.cpp.wire.bindinggroup.test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/wire/server.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

using ::fidl_cpp_wire_bindinggroup_test::Testable;

constexpr auto kNoopCloser = [](fidl::UnbindInfo info) {};
constexpr zx_status_t kTestEpitaph = 1234;
constexpr char kSimpleEcho[] = "test";

struct TestImpl : fidl::Server<Testable> {
 public:
  TestImpl() {}

  void Echo(EchoRequest& request, EchoCompleter::Sync& completer) override {
    echo_count_++;
    completer.Reply(request.str());
  }

  // Always abruptly close the connection. This is not a good implementation to copy - its just
  // useful to check that close handling works properly.
  void Close(CloseCompleter::Sync& completer) override {
    close_count_++;
    completer.Close(kTestEpitaph);
  }

  size_t get_echo_count() const { return echo_count_; }

  size_t get_close_count() const { return close_count_; }

 private:
  size_t close_count_ = 0;
  size_t echo_count_ = 0;
};

TEST(BindingGroup, Trivial) { fidl::ServerBindingGroup<Testable> group; }

// Tests simple patterns for adding various numbers of bindings for various numbers of
// implementations. Additionally, this test template tests that the |size| and |ForEachBinding|
// methods work as expected.
template <size_t NumImpls, size_t NumBindingsPerImpl>
void AddBindingTest() {
  constexpr size_t TotalServerBindings = NumImpls * NumBindingsPerImpl;
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Because we're going to be using raw pointers into these vectors for the test, its important to
  // pre-allocate, so that the underlying storage doesn't move.
  std::vector<TestImpl> impls;
  std::vector<fidl::WireClient<Testable>> clients;
  impls.reserve(NumImpls);
  clients.reserve(TotalServerBindings);

  fidl::ServerBindingGroup<Testable> group;
  std::map<const TestImpl*, size_t> active_bindings_per_impl;

  // Create the right number of bindings for each impl as requested. Hold on to the client so that
  // we may poke at it later.
  for (size_t i = 0; i < NumImpls; i++) {
    impls.emplace_back();
    for (size_t j = i * NumBindingsPerImpl; j < (i + 1) * NumBindingsPerImpl; j++) {
      zx::result<fidl::Endpoints<Testable>> endpoints = fidl::CreateEndpoints<Testable>();
      ASSERT_OK(endpoints.status_value());
      group.AddBinding(loop.dispatcher(), std::move(endpoints->server), &impls.back(), kNoopCloser);
      clients.emplace_back(std::move(endpoints->client), loop.dispatcher());
    }
    active_bindings_per_impl.insert({&impls.back(), NumBindingsPerImpl});
  }
  EXPECT_EQ(group.size(), TotalServerBindings);
  EXPECT_EQ(clients.size(), TotalServerBindings);
  EXPECT_EQ(active_bindings_per_impl.size(), NumImpls);

  // Make an |Echo| call on each |client| to ensure that its binding is actually responsive.
  for (auto& client : clients) {
    client->Echo(kSimpleEcho).ThenExactlyOnce([&](fidl::WireUnownedResult<Testable::Echo>& result) {
      ASSERT_TRUE(result.ok());
      EXPECT_OK(result.status());
      EXPECT_EQ(result.value().str.get(), kSimpleEcho);

      // Quit the loop, thereby handing control back to the outer loop of actions being iterated
      // over.
      loop.Quit();
    });

    // Run the loop until the callback is resolved.
    loop.RunUntilIdle();
    loop.ResetQuit();
  }

  // Ensure that each |impl| was called the number of times that we expect.
  for (const auto& impl : impls) {
    EXPECT_EQ(impl.get_echo_count(), NumBindingsPerImpl);
  }

  // Visit each binding, matching its implementation to one of the ones we're storing in the |impls|
  // array. Decrement the count in that array on each visit.
  size_t bindings_visited = 0;
  group.ForEachBinding([&](const fidl::ServerBinding<Testable>& binding) {
    bool matched = false;
    bindings_visited++;
    binding.AsImpl<TestImpl>([&](const TestImpl* binding_impl) {
      auto matched_impl = active_bindings_per_impl.find(binding_impl);
      EXPECT_NE(matched_impl, active_bindings_per_impl.end());
      matched = true;
      matched_impl->second--;
    });
    EXPECT_TRUE(matched);
  });

  // Because the previous loop decremented the count for each impl visited, we can iterate over the
  // |active_bindings_per_impl| counters to ensure that they are all zero, confirming that every
  // |impl| has been visited the appropriate number of times.
  EXPECT_EQ(bindings_visited, TotalServerBindings);
  for (const auto& impl : active_bindings_per_impl) {
    EXPECT_EQ(impl.second, 0);
  }
}

TEST(BindingGroup, AddBindingOneImplWithOneBinding) { AddBindingTest<1, 1>(); }

TEST(BindingGroup, AddBindingOneImplWithManyBindings) { AddBindingTest<1, 2>(); }

TEST(BindingGroup, AddBindingManyImplsWithOneBindingEach) { AddBindingTest<2, 1>(); }

TEST(BindingGroup, AddBindingManyImplsWithManyBindingsEach) { AddBindingTest<2, 2>(); }

// Tests adding bindings using the generator produced by the |CreateHandler| method.
template <size_t NumImpls, size_t NumBindingsPerImpl>
void CreateHandlerTest() {
  constexpr size_t TotalServerBindings = NumImpls * NumBindingsPerImpl;
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Because we're going to be using raw pointers into these vectors for the test, its important to
  // pre-allocate, so that the underlying storage doesn't move.
  std::vector<TestImpl> impls;
  std::vector<fidl::WireClient<Testable>> clients;
  impls.reserve(NumImpls);
  clients.reserve(TotalServerBindings);

  fidl::ServerBindingGroup<Testable> group;
  std::map<const TestImpl*, size_t> active_bindings_per_impl;

  // Create the right number of bindings for each impl as requested. Hold on to the client so that
  // we may poke at it later.
  for (size_t i = 0; i < NumImpls; i++) {
    impls.emplace_back();
    auto handler = group.CreateHandler(&impls.back(), loop.dispatcher(), kNoopCloser);
    for (size_t j = i * NumBindingsPerImpl; j < (i + 1) * NumBindingsPerImpl; j++) {
      zx::result<fidl::Endpoints<Testable>> endpoints = fidl::CreateEndpoints<Testable>();
      ASSERT_OK(endpoints.status_value());
      handler(std::move(endpoints->server));
      clients.emplace_back(std::move(endpoints->client), loop.dispatcher());
    }
    active_bindings_per_impl.insert({&impls.back(), NumBindingsPerImpl});
  }
  EXPECT_EQ(group.size(), TotalServerBindings);
  EXPECT_EQ(clients.size(), TotalServerBindings);
  EXPECT_EQ(active_bindings_per_impl.size(), NumImpls);

  // Make an |Echo| call on each |client| to ensure that its binding is actually responsive.
  for (auto& client : clients) {
    client->Echo(kSimpleEcho).ThenExactlyOnce([&](fidl::WireUnownedResult<Testable::Echo>& result) {
      ASSERT_TRUE(result.ok());
      EXPECT_OK(result.status());
      EXPECT_EQ(result.value().str.get(), kSimpleEcho);

      // Quit the loop, thereby handing control back to the outer loop of actions being iterated
      // over.
      loop.Quit();
    });

    // Run the loop until the callback is resolved.
    loop.RunUntilIdle();
    loop.ResetQuit();
  }

  // Ensure that each |impl| was called the number of times that we expect.
  for (const auto& impl : impls) {
    EXPECT_EQ(impl.get_echo_count(), NumBindingsPerImpl);
  }
}

TEST(BindingGroup, CreateHandlerOneImplWithOneBinding) { CreateHandlerTest<1, 1>(); }

TEST(BindingGroup, CreateHandlerOneImplWithManyBindings) { CreateHandlerTest<1, 2>(); }

TEST(BindingGroup, CreateHandlerManyImplsWithOneBindingEach) { CreateHandlerTest<2, 1>(); }

TEST(BindingGroup, CreateHandlerManyImplsWithManyBindingsEach) { CreateHandlerTest<2, 2>(); }

// Tests that |CloseHandler| functions are correctly passed to, and fired by, bindings in the group.
template <size_t NumImpls, size_t NumBindingsPerImpl>
void CloseHandlerTest() {
  constexpr size_t TotalServerBindings = NumImpls * NumBindingsPerImpl;
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Because we're going to be using raw pointers into these vectors for the test, its important to
  // pre-allocate, so that the underlying storage doesn't move.
  std::vector<TestImpl> impls;
  std::vector<fidl::WireClient<Testable>> clients;
  impls.reserve(NumImpls);
  clients.reserve(TotalServerBindings);

  fidl::ServerBindingGroup<Testable> group;
  std::map<const TestImpl*, size_t> unclosed_bindings_per_impl;
  auto close_handler = [&](TestImpl* impl, fidl::UnbindInfo info) {
    auto matched_impl = unclosed_bindings_per_impl.find(impl);
    EXPECT_NE(matched_impl, unclosed_bindings_per_impl.end());
    EXPECT_TRUE(info.did_send_epitaph());
    EXPECT_EQ(fidl::Reason::kClose, info.reason());
    EXPECT_OK(info.status());

    matched_impl->second--;
  };

  // Add an |empty_handler| lambda to the |group|, and ensure that it only gets called once.
  size_t empty_set_handler_call_count = 0;
  group.set_empty_set_handler([&]() { empty_set_handler_call_count++; });

  // Create the right number of bindings for each impl as requested. Hold on to the client so that
  // we may poke at it later.
  for (size_t i = 0; i < NumImpls; i++) {
    impls.emplace_back();
    for (size_t j = i * NumBindingsPerImpl; j < (i + 1) * NumBindingsPerImpl; j++) {
      zx::result<fidl::Endpoints<Testable>> endpoints = fidl::CreateEndpoints<Testable>();
      ASSERT_OK(endpoints.status_value());
      group.AddBinding(loop.dispatcher(), std::move(endpoints->server), &impls.back(),
                       close_handler);
      clients.emplace_back(std::move(endpoints->client), loop.dispatcher());
    }
    unclosed_bindings_per_impl.insert({&impls.back(), NumBindingsPerImpl});
  }
  EXPECT_EQ(group.size(), TotalServerBindings);
  EXPECT_EQ(clients.size(), TotalServerBindings);
  EXPECT_EQ(unclosed_bindings_per_impl.size(), NumImpls);

  // Make a |Close| call on each |client| to ensure that it is abruptly torn down.
  for (auto& client : clients) {
    EXPECT_EQ(empty_set_handler_call_count, 0);

    auto result = client->Close();
    EXPECT_TRUE(result.ok());

    // Run the loop until the close handlers are resolved.
    loop.RunUntilIdle();
    loop.ResetQuit();
  }

  // Ensure that each |impl| was closed the number of times that we expect.
  for (const auto& impl : impls) {
    EXPECT_EQ(impl.get_close_count(), NumBindingsPerImpl);
  }

  // Ensure that empty handler was only called once, after the last binding resolved its |Close|
  // handler.
  EXPECT_EQ(empty_set_handler_call_count, 1);
}

TEST(BindingGroup, CloseHandlerOneImplWithOneBinding) { CloseHandlerTest<1, 1>(); }

TEST(BindingGroup, CloseHandlerOneImplWithManyBindings) { CloseHandlerTest<1, 2>(); }

TEST(BindingGroup, CloseHandlerManyImplsWithOneBindingEach) { CloseHandlerTest<2, 1>(); }

TEST(BindingGroup, CloseHandlerManyImplsWithManyBindingsEach) { CloseHandlerTest<2, 2>(); }

}  // namespace
