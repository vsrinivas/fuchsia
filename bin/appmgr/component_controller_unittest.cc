// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"

#include <fs/pseudo-dir.h>
#include <fs/remote-dir.h>
#include <lib/fdio/spawn.h>

#include "gtest/gtest.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/gtest/real_loop_fixture.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;

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

class ComponentControllerTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    gtest::RealLoopFixture::SetUp();

    // create child job
    zx_status_t status = zx::job::create(*zx::job::default_job(), 0u, &job_);
    ASSERT_EQ(status, ZX_OK);

    // create process
    const char* argv[] = {"sh", NULL};
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(job_.get(), FDIO_SPAWN_CLONE_ALL, "/boot/bin/sh",
                            argv, NULL, 0, NULL,
                            process_.reset_and_get_address(), err_msg);
    ASSERT_EQ(status, ZX_OK) << err_msg;
    process_koid_ = std::to_string(fsl::GetKoid(process_.get()));
  }

  void TearDown() override {
    if (job_) {
      job_.kill();
    }
    gtest::RealLoopFixture::TearDown();
  }

 protected:
  std::unique_ptr<ComponentControllerImpl> create_component(
      fuchsia::sys::ComponentControllerPtr& controller,
      ExportedDirType export_dir_type = ExportedDirType::kLegacyFlatLayout,
      zx::channel export_dir = zx::channel()) {
    // job_ is used later in a test to check the job-id, so we need to make a
    // clone of it to pass into std::move
    zx::job job_clone;
    zx_status_t status = job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_clone);
    if (status != ZX_OK)
      return NULL;
    return std::make_unique<ComponentControllerImpl>(
        controller.NewRequest(), &realm_, std::move(job_clone),
        std::move(process_), "test-url", "test-arg", "test-label", nullptr,
        export_dir_type, std::move(export_dir), zx::channel(),
        MakeForwardingTerminationCallback());
  }

  FakeRealm realm_;
  zx::job job_;
  std::string process_koid_;
  zx::process process_;
};

class ComponentBridgeTest : public gtest::RealLoopFixture,
                            public fuchsia::sys::ComponentController {
 public:
  ComponentBridgeTest()
      : binding_(this), binding_error_handler_called_(false) {}
  void SetUp() override {
    gtest::RealLoopFixture::SetUp();
    binding_.Bind(remote_controller_.NewRequest());
    binding_.set_error_handler([this] {
      binding_error_handler_called_ = true;
      Kill();
    });
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
      fuchsia::sys::ComponentControllerPtr& controller,
      ExportedDirType export_dir_type = ExportedDirType::kLegacyFlatLayout,
      zx::channel export_dir = zx::channel()) {
    // only allow creation of one component.
    if (!remote_controller_) {
      return nullptr;
    }
    auto component = std::make_unique<ComponentBridge>(
        controller.NewRequest(), std::move(remote_controller_), &runner_,
        "test-url", "test-arg", "test-label", "1", nullptr, export_dir_type,
        std::move(export_dir), zx::channel(),
        MakeForwardingTerminationCallback());
    component->SetParentJobId(runner_.koid());
    return component;
  }

  void SetReturnCode(int64_t errcode) { errcode_ = errcode; }

  void SendReturnCode() {
    for (const auto& iter : wait_callbacks_) {
      iter(errcode_);
    }
    wait_callbacks_.clear();
    binding_.events().OnTerminated(errcode_, TerminationReason::EXITED);
  }

  std::vector<WaitCallback> wait_callbacks_;
  FakeRunner runner_;
  ::fidl::Binding<fuchsia::sys::ComponentController> binding_;
  fuchsia::sys::ComponentControllerPtr remote_controller_;
  int64_t errcode_ = 1;

  bool binding_error_handler_called_;
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
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = create_component(component_ptr);
  auto hub_info = component->HubInfo();

  EXPECT_EQ(hub_info.label(), "test-label");
  EXPECT_EQ(hub_info.koid(), process_koid_);

  ASSERT_EQ(realm_.ComponentCount(), 0u);
  realm_.AddComponent(std::move(component));

  ASSERT_EQ(realm_.ComponentCount(), 1u);

  bool wait = false;
  int64_t return_code;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&](int64_t err,
                                            TerminationReason reason) {
    return_code = err;
    termination_reason = reason;
  };
  component_ptr->Wait([&wait](int64_t errcode) { wait = true; });
  component_ptr->Kill();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, zx::sec(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(-1, return_code);
  EXPECT_EQ(TerminationReason::EXITED, termination_reason);
  EXPECT_EQ(realm_.ComponentCount(), 0u);
}

TEST_F(ComponentControllerTest, CreateAndDeleteWithoutKilling) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  int64_t return_code = 0;
  TerminationReason termination_reason = TerminationReason::INTERNAL_ERROR;

  auto component = create_component(component_ptr);
  auto* component_to_remove = component.get();
  realm_.AddComponent(std::move(component));
  component_ptr.events().OnTerminated = [&](int64_t err,
                                            TerminationReason reason) {
    return_code = err;
    termination_reason = reason;
  };
  realm_.ExtractComponent(component_to_remove);

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&return_code] { return return_code; },
                                        zx::sec(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(-1, return_code);
  EXPECT_EQ(TerminationReason::UNKNOWN, termination_reason);
  EXPECT_EQ(realm_.ComponentCount(), 0u);
}

TEST_F(ComponentControllerTest, ControllerScope) {
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = create_component(component_ptr);
    realm_.AddComponent(std::move(component));

    ASSERT_EQ(realm_.ComponentCount(), 1u);
  }
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this]() { return realm_.ComponentCount() == 0; }));
}

TEST_F(ComponentControllerTest, DetachController) {
  bool wait = false;
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = create_component(component_ptr);
    component->Wait([&wait](int64_t errcode) { wait = true; });
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

  fuchsia::sys::ComponentControllerPtr component_ptr;

  auto component =
      create_component(component_ptr, ExportedDirType::kPublicDebugCtrlLayout,
                       std::move(export_dir_req));

  EXPECT_STREQ(get_value(component->hub_dir(), "name").c_str(), "test-label");
  EXPECT_STREQ(get_value(component->hub_dir(), "args").c_str(), "test-arg");
  EXPECT_STREQ(get_value(component->hub_dir(), "job-id").c_str(),
               std::to_string(fsl::GetKoid(job_.get())).c_str());
  EXPECT_STREQ(get_value(component->hub_dir(), "url").c_str(), "test-url");
  EXPECT_STREQ(get_value(component->hub_dir(), "process-id").c_str(),
               process_koid_.c_str());
  fbl::RefPtr<fs::Vnode> out_dir;
  ASSERT_TRUE(path_exists(component->hub_dir(), "out", &out_dir));
  ASSERT_TRUE(out_dir->IsRemote());
}

TEST_F(ComponentBridgeTest, CreateAndKill) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = create_component_bridge(component_ptr);
  auto hub_info = component->HubInfo();

  EXPECT_EQ(hub_info.label(), "test-label");

  ASSERT_EQ(runner_.ComponentCount(), 0u);
  runner_.AddComponent(std::move(component));

  ASSERT_EQ(runner_.ComponentCount(), 1u);

  bool wait = false;
  int64_t retval;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&wait, &retval, &termination_reason](
                                            int64_t errcode,
                                            TerminationReason tr) {
    wait = true;
    retval = errcode;
    termination_reason = tr;
  };
  int64_t expected_retval = (1L << 60);
  SetReturnCode(expected_retval);
  component_ptr->Kill();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, zx::sec(5)));
  EXPECT_EQ(expected_retval, retval);
  EXPECT_EQ(TerminationReason::EXITED, termination_reason);

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, CreateAndDeleteWithoutKilling) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = create_component_bridge(component_ptr);
  auto* component_to_remove = component.get();
  component->SetTerminationReason(TerminationReason::INTERNAL_ERROR);
  runner_.AddComponent(std::move(component));

  bool terminated = false;
  int64_t retval = 0;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&](int64_t errcode,
                                            TerminationReason tr) {
    terminated = true;
    retval = errcode;
    termination_reason = tr;
  };
  // Component controller called OnTerminated before the component is destroyed,
  // so we expect the value set above (INTERNAL_ERROR).
  runner_.ExtractComponent(component_to_remove);
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&terminated] { return terminated; },
                                        zx::sec(5)));
  EXPECT_EQ(-1, retval);
  EXPECT_EQ(TerminationReason::INTERNAL_ERROR, termination_reason);

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, RemoteComponentDied) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = create_component_bridge(component_ptr);
  component->SetTerminationReason(TerminationReason::EXITED);
  runner_.AddComponent(std::move(component));

  bool terminated = false;
  int64_t retval = 0;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&](int64_t errcode,
                                            TerminationReason tr) {
    terminated = true;
    retval = errcode;
    termination_reason = tr;
  };
  // Even though the termination reason was set above, unbinding and closing the
  // channel will cause the bridge to return UNKNOWN>.
  binding_.Unbind();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&terminated] { return terminated; },
                                        zx::sec(5)));
  EXPECT_EQ(-1, retval);
  EXPECT_EQ(TerminationReason::UNKNOWN, termination_reason);
  EXPECT_EQ(0u, runner_.ComponentCount());

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, ControllerScope) {
  bool wait = false;
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = create_component_bridge(component_ptr);
    component->Wait([&wait](int64_t errcode) { wait = true; });
    runner_.AddComponent(std::move(component));

    ASSERT_EQ(runner_.ComponentCount(), 1u);
  }
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, zx::sec(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, DetachController) {
  bool wait = false;
  ComponentBridge* component_bridge_ptr;
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = create_component_bridge(component_ptr);
    component->Wait([&wait](int64_t errcode) { wait = true; });
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
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, zx::sec(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, Hub) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  fuchsia::sys::ComponentControllerPtr component_ptr;

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

TEST_F(ComponentBridgeTest, BindingErrorHandler) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  fuchsia::sys::ComponentControllerPtr component_ptr;
  {
    // let it go out of scope, that should trigger binding error handler.
    auto component = create_component_bridge(
        component_ptr, ExportedDirType::kPublicDebugCtrlLayout,
        std::move(export_dir_req));
  }
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this] { return !binding_.is_bound(); },
                                        zx::sec(5)));
  EXPECT_TRUE(binding_error_handler_called_);
}

TEST_F(ComponentBridgeTest, BindingErrorHandlerWhenDetached) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  fuchsia::sys::ComponentControllerPtr component_ptr;
  {
    // let it go out of scope, that should trigger binding error handler.
    auto component = create_component_bridge(
        component_ptr, ExportedDirType::kPublicDebugCtrlLayout,
        std::move(export_dir_req));
    component_ptr->Detach();
    RunLoopUntilIdle();
  }
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this] { return !binding_.is_bound(); },
                                        zx::sec(5)));
  EXPECT_TRUE(binding_error_handler_called_);
}

}  // namespace
}  // namespace component
