// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"

#include <fs/pseudo-dir.h>
#include <fs/remote-dir.h>
#include <lib/fdio/spawn.h>

#include "gtest/gtest.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/gtest/test_with_message_loop.h"

namespace fuchsia {
namespace sys {
namespace {

template <typename T>
class ComponentContainerImpl : public ComponentContainer<T> {
 public:
  size_t ComponentCount() { return components_.size(); }
  const std::string koid() { return "5342"; }

  void AddComponent(std::unique_ptr<T> component);

  std::unique_ptr<T> ExtractComponent(T* controller) override;

 private:
  std::unordered_map<T*, std::unique_ptr<T>> components_;
};

template <typename T>
void ComponentContainerImpl<T>::AddComponent(std::unique_ptr<T> component) {
  auto key = component.get();
  components_.emplace(key, std::move(component));
}

template <typename T>
std::unique_ptr<T> ComponentContainerImpl<T>::ExtractComponent(T* controller) {
  auto it = components_.find(controller);
  if (it == components_.end()) {
    return nullptr;
  }
  auto component = std::move(it->second);

  components_.erase(it);
  return component;
}

typedef ComponentContainerImpl<ComponentControllerImpl> FakeRealm;
typedef ComponentContainerImpl<ComponentBridge> FakeRunner;

class ComponentControllerTest : public gtest::TestWithMessageLoop {
 public:
  void SetUp() override {
    gtest::TestWithMessageLoop::SetUp();

    // create process
    const char* argv[] = {"sh", NULL};
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(
        ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/boot/bin/sh", argv, NULL, 0,
        NULL, process_.reset_and_get_address(), err_msg);
    ASSERT_EQ(status, ZX_OK) << err_msg;
    process_koid_ = std::to_string(fsl::GetKoid(process_.get()));
  }

  void TearDown() override {
    if (process_) {
      process_.kill();
    }
    gtest::TestWithMessageLoop::TearDown();
  }

 protected:
  std::unique_ptr<ComponentControllerImpl> create_component(
      ComponentControllerPtr& controller,
      ExportedDirType export_dir_type = ExportedDirType::kLegacyFlatLayout,
      zx::channel export_dir = zx::channel()) {
    return std::make_unique<ComponentControllerImpl>(
        controller.NewRequest(), &realm_, realm_.koid(), nullptr,
        std::move(process_), "test-url", "test-arg", "test-label", nullptr,
        export_dir_type, std::move(export_dir), zx::channel());
  }

  FakeRealm realm_;
  std::string process_koid_;
  zx::process process_;
};

class ComponentBridgeTest : public gtest::TestWithMessageLoop,
                            public ComponentController {
 public:
  ComponentBridgeTest() : binding_(this) {}
  void SetUp() override {
    gtest::TestWithMessageLoop::SetUp();
    binding_.Bind(remote_controller_.NewRequest());
    binding_.set_error_handler([this] { Kill(); });
  }

  void Kill() override {
    SendReturnCode();
    binding_.Unbind();
  }

  void Wait(WaitCallback callback) override {
    wait_callbacks_.push_back(callback);
    if (!binding_.is_bound()) {
      SendReturnCode();
    }
  }

  void Detach() override { binding_.set_error_handler(nullptr); }

 protected:
  std::unique_ptr<ComponentBridge> create_component_bridge(
      ComponentControllerPtr& controller,
      ExportedDirType export_dir_type = ExportedDirType::kLegacyFlatLayout,
      zx::channel export_dir = zx::channel()) {
    // only allow creation of one component.
    if (!remote_controller_) {
      return nullptr;
    }
    auto component = std::make_unique<ComponentBridge>(
        controller.NewRequest(), std::move(remote_controller_), &runner_,
        nullptr, "test-url", "test-arg", "test-label", "1", nullptr,
        export_dir_type, std::move(export_dir), zx::channel());
    component->SetParentJobId(runner_.koid());
    return component;
  }

  void SendReturnCode() {
    for (const auto& iter : wait_callbacks_) {
      iter(1);
    }
    wait_callbacks_.clear();
  }

  std::vector<WaitCallback> wait_callbacks_;
  FakeRunner runner_;
  ::fidl::Binding<ComponentController> binding_;
  ComponentControllerPtr remote_controller_;
};

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> tokens;
  auto i = 0;
  auto pos = s.find(delim);
  while (pos != std::string::npos) {
    tokens.push_back(s.substr(i, pos - i));
    i = ++pos;
    pos = s.find(delim, pos);
  }
  if (pos == std::string::npos)
    tokens.push_back(s.substr(i, s.length()));
  return tokens;
}

fbl::String get_value(const fbl::RefPtr<fs::PseudoDir>& hub_dir,
                      std::string path) {
  auto tokens = split(path, '/');
  auto ntokens = tokens.size();
  fbl::RefPtr<fs::Vnode> node = hub_dir;
  fbl::RefPtr<fs::Vnode> pdir;
  for (size_t i = 0; i < ntokens; i++) {
    auto token = tokens[i];
    pdir = node;
    if (pdir->Lookup(&node, token) != ZX_OK) {
      EXPECT_FALSE(true) << token << " not found";
      return "";
    }
  }
  fbl::RefPtr<fs::Vnode> file;
  if (node->Open(ZX_FS_RIGHT_READABLE, &file) != ZX_OK) {
    EXPECT_FALSE(true) << "cannot open: " << path;
    return "";
  }
  char buf[1024];
  size_t read_len;
  file->Read(buf, sizeof(buf), 0, &read_len);
  return fbl::String(buf, read_len);
}

bool path_exists(const fbl::RefPtr<fs::PseudoDir>& hub_dir, std::string path,
                 fbl::RefPtr<fs::Vnode>* out = nullptr) {
  auto tokens = split(path, '/');
  auto ntokens = tokens.size();
  fbl::RefPtr<fs::Vnode> dir = hub_dir;
  fbl::RefPtr<fs::Vnode> pdir;
  for (size_t i = 0; i < ntokens; i++) {
    auto token = tokens[i];
    pdir = dir;
    if (pdir->Lookup(&dir, token) != ZX_OK) {
      return false;
    }
  }
  if (out != nullptr) {
    *out = dir;
  }
  return true;
}

TEST_F(ComponentControllerTest, CreateAndKill) {
  ComponentControllerPtr component_ptr;
  auto component = create_component(component_ptr);
  auto hub_info = component->HubInfo();

  EXPECT_EQ(hub_info.label(), "test-label");
  EXPECT_EQ(hub_info.koid(), process_koid_);

  ASSERT_EQ(realm_.ComponentCount(), 0u);
  realm_.AddComponent(std::move(component));

  ASSERT_EQ(realm_.ComponentCount(), 1u);

  bool wait = false;
  component_ptr->Wait([&wait](int errcode) { wait = true; });
  component_ptr->Kill();
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(realm_.ComponentCount(), 0u);
}

TEST_F(ComponentControllerTest, ControllerScope) {
  bool wait = false;
  {
    ComponentControllerPtr component_ptr;
    auto component = create_component(component_ptr);
    component->Wait([&wait](int errcode) { wait = true; });
    realm_.AddComponent(std::move(component));

    ASSERT_EQ(realm_.ComponentCount(), 1u);
  }
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(realm_.ComponentCount(), 0u);
}

TEST_F(ComponentControllerTest, DetachController) {
  bool wait = false;
  {
    ComponentControllerPtr component_ptr;
    auto component = create_component(component_ptr);
    component->Wait([&wait](int errcode) { wait = true; });
    realm_.AddComponent(std::move(component));

    ASSERT_EQ(realm_.ComponentCount(), 1u);

    // detach controller before it goes out of scope and then test that our
    // component did not die.
    component_ptr->Detach();
    RunLoopUntilIdle();
  }

  // make sure all messages are processed if Kill was called.
  RunLoopUntilIdle();
  ASSERT_FALSE(wait);
  EXPECT_EQ(realm_.ComponentCount(), 1u);
}

TEST_F(ComponentControllerTest, Hub) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  ComponentControllerPtr component_ptr;

  auto component =
      create_component(component_ptr, ExportedDirType::kPublicDebugCtrlLayout,
                       std::move(export_dir_req));

  EXPECT_STREQ(get_value(component->hub_dir(), "name").c_str(), "test-label");
  EXPECT_STREQ(get_value(component->hub_dir(), "args").c_str(), "test-arg");
  EXPECT_STREQ(get_value(component->hub_dir(), "job-id").c_str(),
               realm_.koid().c_str());
  EXPECT_STREQ(get_value(component->hub_dir(), "url").c_str(), "test-url");
  EXPECT_STREQ(get_value(component->hub_dir(), "process-id").c_str(),
               process_koid_.c_str());
  fbl::RefPtr<fs::Vnode> out_dir;
  ASSERT_TRUE(path_exists(component->hub_dir(), "out", &out_dir));
  ASSERT_TRUE(out_dir->IsRemote());
}

TEST_F(ComponentBridgeTest, CreateAndKill) {
  ComponentControllerPtr component_ptr;
  auto component = create_component_bridge(component_ptr);
  auto hub_info = component->HubInfo();

  EXPECT_EQ(hub_info.label(), "test-label");

  ASSERT_EQ(runner_.ComponentCount(), 0u);
  runner_.AddComponent(std::move(component));

  ASSERT_EQ(runner_.ComponentCount(), 1u);

  bool wait = false;
  component_ptr->Wait([&wait](int errcode) { wait = true; });
  component_ptr->Kill();
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, ControllerScope) {
  bool wait = false;
  {
    ComponentControllerPtr component_ptr;
    auto component = create_component_bridge(component_ptr);
    component->Wait([&wait](int errcode) { wait = true; });
    runner_.AddComponent(std::move(component));

    ASSERT_EQ(runner_.ComponentCount(), 1u);
  }
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, DetachController) {
  bool wait = false;
  ComponentBridge* component_bridge_ptr;
  {
    ComponentControllerPtr component_ptr;
    auto component = create_component_bridge(component_ptr);
    component->Wait([&wait](int errcode) { wait = true; });
    component_bridge_ptr = component.get();
    runner_.AddComponent(std::move(component));

    ASSERT_EQ(runner_.ComponentCount(), 1u);

    // detach controller before it goes out of scope and then test that our
    // component did not die.
    component_ptr->Detach();
    RunLoopUntilIdle();
  }

  // make sure all messages are processed if Kill was called.
  RunLoopUntilIdle();
  ASSERT_FALSE(wait);
  EXPECT_EQ(runner_.ComponentCount(), 1u);

  // bridge should be still connected, kill that to see if we are able to kill
  // real component.
  component_bridge_ptr->Kill();
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, Hub) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  ComponentControllerPtr component_ptr;

  auto component = create_component_bridge(
      component_ptr, ExportedDirType::kPublicDebugCtrlLayout,
      std::move(export_dir_req));

  EXPECT_STREQ(get_value(component->hub_dir(), "name").c_str(), "test-label");
  EXPECT_STREQ(get_value(component->hub_dir(), "args").c_str(), "test-arg");
  EXPECT_STREQ(get_value(component->hub_dir(), "job-id").c_str(),
               runner_.koid().c_str());
  EXPECT_STREQ(get_value(component->hub_dir(), "url").c_str(), "test-url");
  fbl::RefPtr<fs::Vnode> out_dir;
  ASSERT_TRUE(path_exists(component->hub_dir(), "out", &out_dir));
  ASSERT_TRUE(out_dir->IsRemote());
}

}  // namespace
}  // namespace sys
}  // namespace fuchsia
