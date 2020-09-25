// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls/object.h>

#include <fs/pseudo_dir.h>
#include <fs/pseudo_file.h>
#include <fs/remote_dir.h>
#include <fs/synchronous_vfs.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/gtest/real_loop_fixture.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/util.h"

namespace component {
namespace {

using fuchsia::sys::ServiceList;
using fuchsia::sys::ServiceListPtr;
using fuchsia::sys::TerminationReason;

template <typename T>
class ComponentContainerImpl : public ComponentContainer<T> {
 public:
  size_t ComponentCount() { return components_.size(); }
  const std::string koid() { return "5342"; }

  void AddComponent(std::unique_ptr<T> component);

  std::shared_ptr<T> ExtractComponent(T* controller) override;

 private:
  std::unordered_map<T*, std::shared_ptr<T>> components_;
};

template <typename T>
void ComponentContainerImpl<T>::AddComponent(std::unique_ptr<T> component) {
  auto key = component.get();
  components_.emplace(key, std::move(component));
}

template <typename T>
std::shared_ptr<T> ComponentContainerImpl<T>::ExtractComponent(T* controller) {
  auto it = components_.find(controller);
  if (it == components_.end()) {
    return nullptr;
  }
  auto component = std::move(it->second);

  components_.erase(it);
  return component;
}

// Get a list of the default service entries that exist in every namespace.
// See |Namespace::Namespace| constructor.
std::vector<std::string> GetDefaultNamespaceServiceEntries() {
  return std::vector<std::string>{
      ".",
      Namespace::Launcher::Name_,
      fuchsia::logger::LogSink::Name_,
      fuchsia::process::Launcher::Name_,
      fuchsia::process::Resolver::Name_,
      fuchsia::sys::Environment::Name_,
      fuchsia::sys::internal::LogConnector::Name_,
  };
}

// Create a new Namespace that contains the default services available to all
// namespaces, plus the given |service_names|. The resulting object is useful
// for listing its service names but not much else.
fxl::RefPtr<Namespace> CreateFakeNamespace(const std::vector<const char*>& extra_service_names) {
  ServiceListPtr service_list(new ServiceList());
  for (auto& service : extra_service_names) {
    service_list->names.push_back(service);
  }
  return fxl::MakeRefCounted<Namespace>(nullptr, std::move(service_list), nullptr);
}

std::vector<std::string> SplitPath(const std::string& path) {
  return fxl::SplitStringCopy(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                              fxl::SplitResult::kSplitWantAll);
}

bool PathExists(const fbl::RefPtr<fs::PseudoDir>& hub_dir, std::string path,
                fbl::RefPtr<fs::Vnode>* out = nullptr) {
  auto tokens = SplitPath(path);
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

// Get a list of names of the entries in a directory. This will generally
// include at least "." (i.e. the current directory).
std::vector<std::string> GetDirectoryEntries(fbl::RefPtr<fs::Vnode> dir) {
  std::vector<std::string> entry_names{};
  // Arbitrary size.
  uint8_t buffer[4096];

  fs::vdircookie_t cookie{};
  // Actual number of bytes read into the buffer.
  size_t real_len = 0;
  while (dir->Readdir(&cookie, buffer, sizeof(buffer), &real_len) == ZX_OK && real_len > 0) {
    size_t offset = 0;
    while (offset < real_len) {
      auto dir_entry = reinterpret_cast<vdirent_t*>(buffer + offset);
      size_t entry_size = sizeof(vdirent_t) + dir_entry->size;
      std::string name(dir_entry->name, dir_entry->size);
      entry_names.push_back(name);
      offset += entry_size;
    }
  }
  return entry_names;
}

// Assert that the hub for the given component has "in", "in/svc", default
// services, and the given extra service names.
void AssertHubHasIncomingServices(const ComponentControllerBase* component,
                                  const std::vector<std::string>& extra_service_names) {
  ASSERT_TRUE(PathExists(component->hub_dir(), "in"));
  ASSERT_TRUE(PathExists(component->hub_dir(), "in/pkg"));
  fbl::RefPtr<fs::Vnode> in_svc_dir;
  ASSERT_TRUE(PathExists(component->hub_dir(), "in/svc", &in_svc_dir));
  for (auto& service : extra_service_names) {
    EXPECT_TRUE(PathExists(component->hub_dir(), "in/svc/" + service));
  }

  // Default entries from namespace, plus the two we added.
  auto expected_entries = GetDefaultNamespaceServiceEntries();
  expected_entries.insert(expected_entries.end(), extra_service_names.begin(),
                          extra_service_names.end());
  EXPECT_THAT(GetDirectoryEntries(in_svc_dir),
              ::testing::UnorderedElementsAreArray(expected_entries));
}

typedef ComponentContainerImpl<ComponentControllerImpl> FakeRealm;
typedef ComponentContainerImpl<ComponentBridge> FakeRunner;

class ComponentControllerTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    gtest::RealLoopFixture::SetUp();
    vfs_.SetDispatcher(async_get_default_dispatcher());
    pkg_vfs_.SetDispatcher(async_get_default_dispatcher());

    // create child job
    zx_status_t status = zx::job::create(*zx::job::default_job(), 0u, &job_);
    ASSERT_EQ(status, ZX_OK);

    // create process
    const char* argv[] = {"sleep", "999999999", NULL};
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(job_.get(), FDIO_SPAWN_CLONE_ALL, "/bin/sleep", argv, NULL, 0, NULL,
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
  std::unique_ptr<ComponentControllerImpl> CreateComponent(
      fuchsia::sys::ComponentControllerPtr& controller, zx::channel export_dir = zx::channel(),
      zx::channel pkg_dir = zx::channel(), fxl::RefPtr<Namespace> ns = CreateFakeNamespace({})) {
    // job_ is used later in a test to check the job-id, so we need to make a
    // clone of it to pass into std::move
    zx::job job_clone;
    zx_status_t status = job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_clone);
    if (status != ZX_OK) {
      return nullptr;
    }
    return std::make_unique<ComponentControllerImpl>(
        controller.NewRequest(), &realm_, std::move(job_clone), std::move(process_), "test-url",
        "test-arg", "test-label", ns, std::move(export_dir), zx::channel(), std::move(pkg_dir));
  }

  FakeRealm realm_;
  zx::job job_;
  std::string process_koid_;
  zx::process process_;
  fs::SynchronousVfs vfs_;
  fs::SynchronousVfs pkg_vfs_;
};

class ComponentBridgeTest : public gtest::RealLoopFixture,
                            public fuchsia::sys::ComponentController {
 public:
  ComponentBridgeTest() : binding_(this), binding_error_handler_called_(false) {}
  void SetUp() override {
    gtest::RealLoopFixture::SetUp();
    vfs_.SetDispatcher(async_get_default_dispatcher());
    pkg_vfs_.SetDispatcher(async_get_default_dispatcher());
    binding_.Bind(remote_controller_.NewRequest());
    binding_.set_error_handler([this](zx_status_t status) {
      binding_error_handler_called_ = true;
      Kill();
    });
  }

  void Kill() override {
    SendReturnCode();
    binding_.Unbind();
  }

  void Detach() override { binding_.set_error_handler(nullptr); }

 protected:
  std::unique_ptr<ComponentBridge> CreateComponentBridge(
      fuchsia::sys::ComponentControllerPtr& controller, zx::channel export_dir = zx::channel(),
      zx::channel package_handle = zx::channel(),
      fxl::RefPtr<Namespace> ns = CreateFakeNamespace({})) {
    // only allow creation of one component.
    if (!remote_controller_) {
      return nullptr;
    }
    auto component = std::make_unique<ComponentBridge>(
        controller.NewRequest(), std::move(remote_controller_), &runner_, "test-url", "test-arg",
        "test-label", "1", ns, std::move(export_dir), zx::channel(), std::move(package_handle));
    component->SetParentJobId(runner_.koid());
    return component;
  }

  void SetReturnCode(int64_t errcode) { errcode_ = errcode; }

  void SendReady() { binding_.events().OnDirectoryReady(); }

  void SendReturnCode() { binding_.events().OnTerminated(errcode_, TerminationReason::EXITED); }

  FakeRunner runner_;
  ::fidl::Binding<fuchsia::sys::ComponentController> binding_;
  fuchsia::sys::ComponentControllerPtr remote_controller_;
  fs::SynchronousVfs vfs_;
  fs::SynchronousVfs pkg_vfs_;
  int64_t errcode_ = 1;

  bool binding_error_handler_called_;
};

fbl::String get_value(const fbl::RefPtr<fs::PseudoDir>& hub_dir, std::string path) {
  auto tokens = SplitPath(path);
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
  auto validated_options = node->ValidateOptions(fs::VnodeConnectionOptions::ReadOnly());
  if (validated_options.is_error()) {
    EXPECT_FALSE(true) << "validate option failed: " << validated_options.error();
  }
  if (node->Open(validated_options.value(), &file) != ZX_OK) {
    EXPECT_FALSE(true) << "cannot open: " << path;
    return "";
  }
  char buf[1024];
  size_t read_len;
  file->Read(buf, sizeof(buf), 0, &read_len);
  return fbl::String(buf, read_len);
}

TEST_F(ComponentControllerTest, CreateAndKill) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = CreateComponent(component_ptr);
  auto hub_info = component->HubInfo();

  EXPECT_EQ(hub_info.label(), "test-label");
  EXPECT_EQ(hub_info.koid(), process_koid_);

  ASSERT_EQ(realm_.ComponentCount(), 0u);
  realm_.AddComponent(std::move(component));

  ASSERT_EQ(realm_.ComponentCount(), 1u);

  bool wait = false;
  int64_t return_code;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&](int64_t err, TerminationReason reason) {
    return_code = err;
    termination_reason = reason;
    wait = true;
  };
  component_ptr->Kill();
  RunLoopUntil([&wait] { return wait; });

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_TASK_RETCODE_SYSCALL_KILL, return_code);

  EXPECT_EQ(TerminationReason::EXITED, termination_reason);
  EXPECT_EQ(realm_.ComponentCount(), 0u);
}

TEST_F(ComponentControllerTest, CreateAndDeleteWithoutKilling) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  int64_t return_code = 0;
  TerminationReason termination_reason = TerminationReason::INTERNAL_ERROR;

  auto component = CreateComponent(component_ptr);
  auto* component_to_remove = component.get();
  realm_.AddComponent(std::move(component));
  component_ptr.events().OnTerminated = [&](int64_t err, TerminationReason reason) {
    return_code = err;
    termination_reason = reason;
  };
  realm_.ExtractComponent(component_to_remove);

  RunLoopUntil([&return_code] { return return_code; });

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(-1, return_code);
  EXPECT_EQ(TerminationReason::UNKNOWN, termination_reason);
  EXPECT_EQ(realm_.ComponentCount(), 0u);
}

TEST_F(ComponentControllerTest, ControllerScope) {
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = CreateComponent(component_ptr);
    realm_.AddComponent(std::move(component));

    ASSERT_EQ(realm_.ComponentCount(), 1u);
  }
  RunLoopUntil([this]() { return realm_.ComponentCount() == 0; });
}

TEST_F(ComponentControllerTest, DetachController) {
  syslog::SetLogSettings({.min_log_level = -2});
  bool wait = false;
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = CreateComponent(component_ptr);
    component_ptr.events().OnTerminated = [&](int64_t return_code,
                                              TerminationReason termination_reason) {
      FX_LOGS(ERROR) << "OnTerminated called: " << return_code
                     << ", : " << static_cast<uint32_t>(termination_reason);
      wait = true;
    };
    realm_.AddComponent(std::move(component));

    ASSERT_EQ(realm_.ComponentCount(), 1u);

    // detach controller before it goes out of scope and then test that our
    // component did not die.
    component_ptr->Detach();
    RunLoopUntilIdle();
    EXPECT_FALSE(wait) << "Please please please report logs from this failure to fxbug.dev/8292.";
  }

  // make sure all messages are processed if Kill was called.
  RunLoopUntilIdle();
  EXPECT_FALSE(wait) << "Please please please report logs from this failure to fxbug.dev/8292.";
  EXPECT_EQ(realm_.ComponentCount(), 1u)
      << "Please please please report logs from this failure to fxbug.dev/8292.";
}

TEST_F(ComponentControllerTest, Hub) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);
  vfs_.ServeDirectory(fbl::MakeRefCounted<fs::PseudoDir>(), std::move(export_dir));

  fuchsia::sys::ComponentControllerPtr component_ptr;

  zx::channel pkg_dir, pkg_dir_req;
  ASSERT_EQ(zx::channel::create(0, &pkg_dir, &pkg_dir_req), ZX_OK);
  auto component =
      CreateComponent(component_ptr, std::move(export_dir_req), std::move(pkg_dir_req));

  bool ready = false;
  component_ptr.events().OnDirectoryReady = [&ready] { ready = true; };
  RunLoopUntil([&ready] { return ready; });

  EXPECT_TRUE(PathExists(component->hub_dir(), "out"));
  EXPECT_STREQ(get_value(component->hub_dir(), "name").c_str(), "test-label");
  EXPECT_STREQ(get_value(component->hub_dir(), "args").c_str(), "test-arg");
  EXPECT_STREQ(get_value(component->hub_dir(), "job-id").c_str(),
               std::to_string(fsl::GetKoid(job_.get())).c_str());
  EXPECT_STREQ(get_value(component->hub_dir(), "url").c_str(), "test-url");
  EXPECT_STREQ(get_value(component->hub_dir(), "process-id").c_str(), process_koid_.c_str());

  // "in", "in/svc", and default services should exist.
  AssertHubHasIncomingServices(component.get(), {});

  fbl::RefPtr<fs::Vnode> out_dir;
  ASSERT_TRUE(PathExists(component->hub_dir(), "out", &out_dir));
  ASSERT_TRUE(out_dir->IsRemote());
}

TEST_F(ComponentControllerTest, HubWithIncomingServices) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);
  vfs_.ServeDirectory(fbl::MakeRefCounted<fs::PseudoDir>(), std::move(export_dir));

  fuchsia::sys::ComponentControllerPtr component_ptr;

  fxl::RefPtr<Namespace> ns = CreateFakeNamespace({"service_a", "service_b"});

  zx::channel pkg_dir, pkg_dir_req;
  ASSERT_EQ(zx::channel::create(0, &pkg_dir, &pkg_dir_req), ZX_OK);
  auto component =
      CreateComponent(component_ptr, std::move(export_dir_req), std::move(pkg_dir_req), ns);

  bool ready = false;
  component_ptr.events().OnDirectoryReady = [&ready] { ready = true; };
  RunLoopUntil([&ready] { return ready; });

  AssertHubHasIncomingServices(component.get(), {"service_a", "service_b"});
}

TEST_F(ComponentControllerTest, GetDiagnosticsDirExists) {
  auto out_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);
  vfs_.ServeDirectory(out_dir, std::move(export_dir));
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = CreateComponent(component_ptr, std::move(export_dir_req));

  bool ready = false;
  component_ptr.events().OnDirectoryReady = [&ready] { ready = true; };
  RunLoopUntil([&ready] { return ready; });

  auto diagnostics_dir = new fs::PseudoDir();
  auto test_file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());
  ASSERT_EQ(ZX_OK, diagnostics_dir->AddEntry("test_file", test_file));
  ASSERT_EQ(ZX_OK,
            out_dir->AddEntry("diagnostics", fbl::AdoptRef<fs::Vnode>(std::move(diagnostics_dir))));

  bool done = false;
  async::Executor executor(async_get_default_dispatcher());
  fidl::InterfaceHandle<fuchsia::io::Directory> directory_handle;
  executor.schedule_task(component->GetDiagnosticsDir().then(
      [&](fit::result<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t>& result) {
        EXPECT_TRUE(result.is_ok());
        directory_handle = std::move(result.value());
        done = true;
      }));

  RunLoopUntil([&done] { return done; });

  // Test the directory contains only our test file.
  fuchsia::io::DirectoryPtr directory = directory_handle.Bind();
  std::vector<std::string> entry_names;

  zx_status_t status;
  std::vector<uint8_t> buffer;
  done = false;
  directory->ReadDirents(
      fuchsia::io::MAX_BUF,
      [&status, &buffer, &done](zx_status_t read_status, std::vector<uint8_t> read_buffer) {
        buffer = std::move(read_buffer);
        status = std::move(read_status);
        done = true;
      });

  RunLoopUntil([&done] { return done; });

  EXPECT_EQ(status, ZX_OK);
  EXPECT_FALSE(buffer.empty());

  size_t offset = 0;
  auto data_ptr = buffer.data();

  while (sizeof(vdirent_t) < buffer.size() - offset) {
    vdirent_t* entry = reinterpret_cast<vdirent_t*>(data_ptr + offset);
    std::string name(entry->name, entry->size);
    entry_names.push_back(name);
    offset += sizeof(vdirent_t) + entry->size;
  }

  EXPECT_THAT(entry_names, testing::UnorderedElementsAre(".", "test_file"));
}

TEST_F(ComponentControllerTest, GetDiagnosticsDirMissing) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);
  vfs_.ServeDirectory(fbl::MakeRefCounted<fs::PseudoDir>(), std::move(export_dir));

  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = CreateComponent(component_ptr, std::move(export_dir_req));

  bool ready = false;
  component_ptr.events().OnDirectoryReady = [&ready] { ready = true; };
  RunLoopUntil([&ready] { return ready; });

  bool done = false;
  async::Executor executor(async_get_default_dispatcher());
  executor.schedule_task(component->GetDiagnosticsDir().then(
      [&](fit::result<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t>& result) {
        EXPECT_TRUE(result.is_error());
        done = true;
      }));
  RunLoopUntil([&done] { return done; });
}

TEST_F(ComponentBridgeTest, CreateAndKill) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = CreateComponentBridge(component_ptr);
  auto hub_info = component->HubInfo();

  EXPECT_EQ(hub_info.label(), "test-label");

  ASSERT_EQ(runner_.ComponentCount(), 0u);
  runner_.AddComponent(std::move(component));

  ASSERT_EQ(runner_.ComponentCount(), 1u);

  bool wait = false;
  bool ready = false;
  int64_t retval;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&wait, &retval, &termination_reason](
                                            int64_t errcode, TerminationReason tr) {
    wait = true;
    retval = errcode;
    termination_reason = tr;
  };
  component_ptr.events().OnDirectoryReady = [&ready] { ready = true; };
  int64_t expected_retval = (1L << 60);
  SendReady();
  SetReturnCode(expected_retval);
  component_ptr->Kill();
  RunLoopUntil([&wait] { return wait; });
  EXPECT_TRUE(ready);
  EXPECT_EQ(expected_retval, retval);
  EXPECT_EQ(TerminationReason::EXITED, termination_reason);

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, CreateAndDeleteWithoutKilling) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = CreateComponentBridge(component_ptr);
  auto* component_to_remove = component.get();
  component->SetTerminationReason(TerminationReason::INTERNAL_ERROR);
  runner_.AddComponent(std::move(component));

  bool terminated = false;
  int64_t retval = 0;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&](int64_t errcode, TerminationReason tr) {
    terminated = true;
    retval = errcode;
    termination_reason = tr;
  };
  // Component controller called OnTerminated before the component is destroyed,
  // so we expect the value set above (INTERNAL_ERROR).
  runner_.ExtractComponent(component_to_remove);
  RunLoopUntil([&terminated] { return terminated; });
  EXPECT_EQ(-1, retval);
  EXPECT_EQ(TerminationReason::INTERNAL_ERROR, termination_reason);

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, RemoteComponentDied) {
  fuchsia::sys::ComponentControllerPtr component_ptr;
  auto component = CreateComponentBridge(component_ptr);
  component->SetTerminationReason(TerminationReason::EXITED);
  runner_.AddComponent(std::move(component));

  bool terminated = false;
  int64_t retval = 0;
  TerminationReason termination_reason;
  component_ptr.events().OnTerminated = [&](int64_t errcode, TerminationReason tr) {
    terminated = true;
    retval = errcode;
    termination_reason = tr;
  };
  // Even though the termination reason was set above, unbinding and closing the
  // channel will cause the bridge to return UNKNOWN>.
  binding_.Unbind();
  RunLoopUntil([&terminated] { return terminated; });
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
    auto component = CreateComponentBridge(component_ptr);
    component->OnTerminated(
        [&wait](int64_t return_code, TerminationReason termination_reason) { wait = true; });
    runner_.AddComponent(std::move(component));
    ASSERT_EQ(runner_.ComponentCount(), 1u);
  }
  RunLoopUntil([&wait] { return wait; });

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, DetachController) {
  bool wait = false;
  ComponentBridge* component_bridge_ptr;
  {
    fuchsia::sys::ComponentControllerPtr component_ptr;
    auto component = CreateComponentBridge(component_ptr);
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
  component_bridge_ptr->OnTerminated(
      [&wait](int64_t return_code, TerminationReason termination_reason) { wait = true; });
  component_bridge_ptr->Kill();
  RunLoopUntil([&wait] { return wait; });

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(runner_.ComponentCount(), 0u);
}

TEST_F(ComponentBridgeTest, Hub) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);
  vfs_.ServeDirectory(fbl::MakeRefCounted<fs::PseudoDir>(), std::move(export_dir));

  fuchsia::sys::ComponentControllerPtr component_ptr;

  zx::channel pkg_dir, pkg_dir_req;
  ASSERT_EQ(zx::channel::create(0, &pkg_dir, &pkg_dir_req), ZX_OK);
  auto component =
      CreateComponentBridge(component_ptr, std::move(export_dir_req), std::move(pkg_dir_req));

  RunLoopUntil([&component] { return PathExists(component->hub_dir(), "out"); });

  EXPECT_STREQ(get_value(component->hub_dir(), "name").c_str(), "test-label");
  EXPECT_STREQ(get_value(component->hub_dir(), "args").c_str(), "test-arg");
  EXPECT_STREQ(get_value(component->hub_dir(), "job-id").c_str(), runner_.koid().c_str());
  EXPECT_STREQ(get_value(component->hub_dir(), "url").c_str(), "test-url");
  fbl::RefPtr<fs::Vnode> out_dir;
  ASSERT_TRUE(PathExists(component->hub_dir(), "out", &out_dir));
  ASSERT_TRUE(out_dir->IsRemote());

  // "in", "in/svc", and default services should exist.
  AssertHubHasIncomingServices(component.get(), {});
}

TEST_F(ComponentBridgeTest, HubWithIncomingServices) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);
  vfs_.ServeDirectory(fbl::MakeRefCounted<fs::PseudoDir>(), std::move(export_dir));

  fuchsia::sys::ComponentControllerPtr component_ptr;

  fxl::RefPtr<Namespace> ns = CreateFakeNamespace({"service_a", "service_b"});

  zx::channel pkg_dir, pkg_dir_req;
  ASSERT_EQ(zx::channel::create(0, &pkg_dir, &pkg_dir_req), ZX_OK);
  auto component =
      CreateComponentBridge(component_ptr, std::move(export_dir_req), std::move(pkg_dir_req), ns);

  RunLoopUntil([&component] { return PathExists(component->hub_dir(), "out"); });

  AssertHubHasIncomingServices(component.get(), {"service_a", "service_b"});
}

TEST_F(ComponentBridgeTest, BindingErrorHandler) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  fuchsia::sys::ComponentControllerPtr component_ptr;
  {
    // let it go out of scope, that should trigger binding error handler.
    auto component = CreateComponentBridge(component_ptr, std::move(export_dir_req));
  }
  RunLoopUntil([this] { return !binding_.is_bound(); });
  EXPECT_TRUE(binding_error_handler_called_);
}

TEST_F(ComponentBridgeTest, BindingErrorHandlerWhenDetached) {
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  fuchsia::sys::ComponentControllerPtr component_ptr;
  {
    // let it go out of scope, that should trigger binding error handler.
    auto component = CreateComponentBridge(component_ptr, std::move(export_dir_req));
    component_ptr->Detach();
    RunLoopUntilIdle();
  }
  RunLoopUntil([this] { return !binding_.is_bound(); });
  EXPECT_TRUE(binding_error_handler_called_);
}

TEST(ComponentControllerUnitTest, GetDirectoryEntries) {
  auto dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
  auto subdir = fbl::AdoptRef<fs::Vnode>(new fs::PseudoDir());
  auto file1 = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());
  auto file2 = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());

  // add entries
  EXPECT_EQ(ZX_OK, dir->AddEntry("subdir", subdir));
  EXPECT_EQ(ZX_OK, dir->AddEntry("file1", file1));
  EXPECT_EQ(ZX_OK, dir->AddEntry("file2", file2));

  const std::vector<std::string> entries = GetDirectoryEntries(dir);
  EXPECT_THAT(entries, ::testing::ElementsAre(".", "subdir", "file1", "file2"));
}

}  // namespace
}  // namespace component
