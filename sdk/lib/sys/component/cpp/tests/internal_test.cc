// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/data/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/internal/convert.h>
#include <lib/sys/component/cpp/testing/internal/local_component_runner.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/component/cpp/tests/utils.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <cstddef>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>
#include <test/placeholders/cpp/fidl.h>

namespace {

// NOLINTNEXTLINE
using namespace component_testing;
// NOLINTNEXTLINE
using namespace component_testing::internal;

namespace fctest = fuchsia::component::test;
namespace fcdecl = fuchsia::component::decl;
namespace fio = fuchsia::io;

/*
 * Tests for |component_testing::internal::LocalComponentRunner| and
 * |component_testing::internal::LocalComponentRunner::Builder|.
 */

class TestComponent : public LocalComponent {
 public:
  explicit TestComponent(fit::closure quit_loop) : quit_loop_(std::move(quit_loop)) {}

  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    mock_handles_ = std::move(mock_handles);
    called_ = true;
    quit_loop_();
  }

  LocalComponentHandles* GetMockHandles() { return mock_handles_.get(); }

  bool WasCalled() const { return called_; }

 private:
  fit::closure quit_loop_;
  std::unique_ptr<LocalComponentHandles> mock_handles_ = nullptr;
  bool called_ = false;
};

class LocalComponentRunnerTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    auto builder = LocalComponentRunner::Builder();
    test_component_ = std::make_unique<TestComponent>(QuitLoopClosure());
    builder.Register(kTestComponentName, test_component_.get());
    local_component_runner_ = builder.Build(dispatcher());
    runner_proxy_.Bind(local_component_runner_->NewBinding());
  }

  void TearDown() override {
    local_component_runner_.reset();
    test_component_.reset();
    default_component_controller_.Unbind();
    runner_proxy_.Unbind();
  }

 protected:
  static constexpr char kTestComponentName[] = "test_component";

  void CallStart(
      fuchsia::component::runner::ComponentStartInfo start_info,
      fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) {
    runner_proxy_->Start(std::move(start_info), std::move(controller));
  }

  void CallStart(fuchsia::component::runner::ComponentStartInfo start_info) {
    CallStart(std::move(start_info), default_component_controller_.NewRequest(dispatcher()));
  }

  TestComponent* GetTestComponent() { return test_component_.get(); }

  static fuchsia::component::runner::ComponentStartInfo CreateValidStartInfo() {
    fuchsia::component::runner::ComponentStartInfo start_info;
    start_info.set_program(CreateValidProgram());
    return start_info;
  }

  static fuchsia::data::Dictionary CreateValidProgram() {
    fuchsia::data::Dictionary program;
    fuchsia::data::DictionaryEntry entry;
    entry.key = fctest::LOCAL_COMPONENT_NAME_KEY;
    auto value = fuchsia::data::DictionaryValue::New();
    value->set_str(kTestComponentName);
    entry.value = std::move(value);
    program.mutable_entries()->push_back(std::move(entry));
    return program;
  }

 private:
  std::unique_ptr<LocalComponentRunner> local_component_runner_ = nullptr;
  std::unique_ptr<TestComponent> test_component_ = nullptr;
  fuchsia::component::runner::ComponentControllerPtr default_component_controller_;
  fuchsia::component::runner::ComponentRunnerPtr runner_proxy_;
};

TEST_F(LocalComponentRunnerTest, RunnerStartsOnStartRequest) {
  CallStart(CreateValidStartInfo());

  RunLoop();

  EXPECT_TRUE(GetTestComponent()->WasCalled());
}

class EchoImpl : public test::placeholders::Echo {
 public:
  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
  }
};

TEST_F(LocalComponentRunnerTest, RunnerGivesComponentItsOutgoingDir) {
  auto start_info = CreateValidStartInfo();
  fuchsia::io::DirectoryPtr outgoing_directory_proxy;
  start_info.set_outgoing_dir(outgoing_directory_proxy.NewRequest(dispatcher()));
  EchoImpl echo_impl;
  test::placeholders::EchoPtr echo_proxy;
  fidl::Binding<test::placeholders::Echo> echo_binding(&echo_impl);

  CallStart(std::move(start_info));
  RunLoop();

  auto mock_handles = GetTestComponent()->GetMockHandles();
  ASSERT_TRUE(mock_handles != nullptr);
  ASSERT_EQ(mock_handles->outgoing()->AddPublicService<test::placeholders::Echo>(
                [&](fidl::InterfaceRequest<test::placeholders::Echo> request) {
                  echo_binding.Bind(std::move(request));
                }),
            ZX_OK);
  echo_proxy.Bind(echo_binding.NewBinding(dispatcher()));

  bool echoed = false;
  echo_proxy->EchoString("hello", [&](fidl::StringPtr _) { echoed = true; });
  RunLoopUntil([&]() { return echoed; });
  EXPECT_TRUE(echoed);
}

TEST_F(LocalComponentRunnerTest, RunnerGivesComponentItsNamespace) {
  static constexpr char kNamespacePath[] = "/test/path";

  auto start_info = CreateValidStartInfo();
  fuchsia::component::runner::ComponentNamespaceEntry ns_entry;
  ns_entry.set_path(kNamespacePath);
  zx::channel e1, e2;
  ASSERT_EQ(zx::channel::create(0, &e1, &e2), ZX_OK);
  // We'll provide a valid channel to and verify that it's present down below.
  ns_entry.set_directory(fidl::InterfaceHandle<fuchsia::io::Directory>(std::move(e2)));
  start_info.mutable_ns()->push_back(std::move(ns_entry));

  CallStart(std::move(start_info));
  RunLoop();

  auto mock_handles = GetTestComponent()->GetMockHandles();
  ASSERT_TRUE(mock_handles != nullptr);
  EXPECT_TRUE(fdio_ns_is_bound(mock_handles->ns(), kNamespacePath));
}

/*
 * Tests for |component_testing::internal::Convert*| functions.
 */

template <typename InputType, typename OutputType>
class ConvertParameterizedFixture
    : public testing::TestWithParam<std::pair<InputType, std::shared_ptr<OutputType>>> {};

#define ZX_COMPONENT_TYPED_TEST_P(test_suite_name, method_name) \
  TEST_P(test_suite_name, method_name) {                        \
    auto param = GetParam();                                    \
    auto actual = ConvertToFidl(param.first);                   \
    auto expected = std::move(*param.second);                   \
    EXPECT_TRUE(fidl::Equals(actual, expected));                \
  }

class ConvertChildOptionsParameterizedFixture
    : public ConvertParameterizedFixture<ChildOptions, fctest::ChildOptions> {};

ZX_COMPONENT_TYPED_TEST_P(ConvertChildOptionsParameterizedFixture, ConvertsAllFields)
INSTANTIATE_TEST_SUITE_P(
    ConvertTest, ConvertChildOptionsParameterizedFixture,
    testing::Values(
        std::make_pair(ChildOptions{.startup_mode = StartupMode::EAGER, .environment = "foo"},
                       component::tests::CreateFidlChildOptions(StartupMode::EAGER, "foo")),
        std::make_pair(ChildOptions{.startup_mode = StartupMode::LAZY, .environment = "bar"},
                       component::tests::CreateFidlChildOptions(StartupMode::LAZY, "bar"))));

class ConvertRefParameterizedFixture : public ConvertParameterizedFixture<Ref, fcdecl::Ref> {};

ZX_COMPONENT_TYPED_TEST_P(ConvertRefParameterizedFixture, ConvertsAllFields)
INSTANTIATE_TEST_SUITE_P(
    ConvertTest, ConvertRefParameterizedFixture,
    testing::Values(
        std::make_pair(Ref{ParentRef{}},
                       std::make_shared<fcdecl::Ref>(fcdecl::Ref::WithParent(fcdecl::ParentRef{}))),
        std::make_pair(ChildRef{.name = "foo"}, component::tests::CreateFidlChildRef("foo"))));

class ConvertCapabilityParameterizedFixture
    : public ConvertParameterizedFixture<Capability, fctest::Capability2> {};

ZX_COMPONENT_TYPED_TEST_P(ConvertCapabilityParameterizedFixture, ConvertsAllField)
INSTANTIATE_TEST_SUITE_P(
    ConvertTest, ConvertCapabilityParameterizedFixture,
    testing::Values(
        std::make_pair(Protocol{"foo"},
                       component::tests::CreateFidlProtocolCapability(/*name=*/"foo")),
        std::make_pair(
            Protocol{.name = "foo",
                     .as = "bar",
                     .type = fcdecl::DependencyType::WEAK,
                     .path = "/foo/bar/baz"},
            component::tests::CreateFidlProtocolCapability(/*name=*/"foo", /*as=*/"bar",
                                                           /*type=*/fcdecl::DependencyType::WEAK,
                                                           /*path=*/"/foo/bar/baz")),
        std::make_pair(Service{"foo"},
                       component::tests::CreateFidlServiceCapability(/*name=*/"foo")),
        std::make_pair(Service{.name = "foo", .as = "bar", .path = "/foo/bar/baz"},
                       component::tests::CreateFidlServiceCapability(/*name=*/"foo", /*as=*/"bar",
                                                                     /*path=*/"/foo/bar/baz")),
        std::make_pair(Directory{"foo"},
                       component::tests::CreateFidlDirectoryCapability(/*name=*/"foo")),
        std::make_pair(
            Directory{.name = "foo",
                      .as = "bar",
                      .type = fcdecl::DependencyType::WEAK,
                      .subdir = "baz",
                      .rights = fio::Operations::EXECUTE,
                      .path = "/foo/bar/baz"},
            component::tests::CreateFidlDirectoryCapability(/*name=*/"foo", /*as=*/"bar",
                                                            /*type=*/fcdecl::DependencyType::WEAK,
                                                            /*subdir=*/"baz",
                                                            /*rights=*/fio::Operations::EXECUTE,
                                                            /*path=*/"/foo/bar/baz"))));

class ConvertTest : public testing::Test {};

TEST_F(ConvertTest, ToFidlVec) {
  std::vector<fcdecl::Ref> actual_values =
      ConvertToFidlVec<Ref, fcdecl::Ref>({ChildRef{"foo"}, ChildRef{"bar"}});
  std::vector<std::shared_ptr<fcdecl::Ref>> expected_values = {
      component::tests::CreateFidlChildRef("foo"), component::tests::CreateFidlChildRef("bar")};

  ZX_ASSERT(actual_values.size() == expected_values.size());
  for (size_t i = 0; i < expected_values.size(); ++i) {
    auto& actual = actual_values[i];
    auto& expected = (*expected_values[i]);
    EXPECT_TRUE(fidl::Equals(actual, expected));
  }
}

}  // namespace
