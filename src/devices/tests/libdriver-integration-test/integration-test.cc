// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "integration-test.h"

#include <fcntl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fit/bridge.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

namespace libdriver_integration_test {

IntegrationTest::IsolatedDevmgr IntegrationTest::devmgr_;

void IntegrationTest::SetUpTestCase() { DoSetup(false /* should_create_composite */); }

void IntegrationTest::DoSetup(bool should_create_composite) {
  // Set up the isolated devmgr instance for this test suite.  Note that we
  // only do this once for the whole suite, because it is currently an
  // expensive process.  Ideally we'd do this between every test.
  auto args = IsolatedDevmgr::DefaultArgs();
  args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));
  args.load_drivers.push_back("/boot/driver/component.so");
  args.load_drivers.push_back("/boot/driver/component.proxy.so");

  // Rig up a get_boot_item that will send configuration information over to
  // the sysdev driver.
  args.get_boot_item = [should_create_composite](uint32_t type, uint32_t extra, zx::vmo* vmo,
                                                 uint32_t* length) {
    vmo->reset();
    *length = 0;
    if (type != ZBI_TYPE_DRV_BOARD_PRIVATE || extra != 0) {
      return ZX_OK;
    }
    zx::vmo data;
    zx_status_t status = zx::vmo::create(1, 0, &data);
    if (status != ZX_OK) {
      return status;
    }
    status = data.write(reinterpret_cast<const void*>(&should_create_composite), 0,
                        sizeof(should_create_composite));
    if (status != ZX_OK) {
      return status;
    }
    *length = sizeof(should_create_composite);
    *vmo = std::move(data);
    return ZX_OK;
  };

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &IntegrationTest::devmgr_);
  ASSERT_EQ(status, ZX_OK) << "failed to create IsolatedDevmgr";
}

void IntegrationTest::TearDownTestCase() { IntegrationTest::devmgr_.reset(); }

IntegrationTest::IntegrationTest()
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), devmgr_exception_(this) {}

void IntegrationTest::SetUp() {
  // We do this in SetUp() rather than the ctor, since gtest cannot assert in
  // ctors.
  zx_status_t status =
      devmgr_.containing_job().create_exception_channel(0, &devmgr_exception_channel_);
  ASSERT_EQ(status, ZX_OK) << "failed to create isolated devmgr exception channel";

  devmgr_exception_.set_object(devmgr_exception_channel_.get());
  devmgr_exception_.set_trigger(ZX_CHANNEL_READABLE);
  status = devmgr_exception_.Begin(loop_.dispatcher());
  ASSERT_EQ(status, ZX_OK) << "failed to watch isolated devmgr for crashes: "
                           << zx_status_get_string(status);

  fdio_t* io = fdio_unsafe_fd_to_io(IntegrationTest::devmgr_.devfs_root().get());
  status = devfs_.Bind(zx::channel(fdio_service_clone(fdio_unsafe_borrow_channel(io))),
                       loop_.dispatcher());
  fdio_unsafe_release(io);
  ASSERT_EQ(status, ZX_OK) << "failed to connect to devfs";
}

IntegrationTest::~IntegrationTest() = default;

void IntegrationTest::DevmgrException(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
  // Log an error in the currently running test
  ADD_FAILURE() << "Crash inside devmgr job";
  loop_.Quit();
}

void IntegrationTest::RunPromise(Promise<void> promise) {
  async::Executor executor(loop_.dispatcher());

  auto new_promise = promise.then([&](Promise<void>::result_type& result) {
    if (result.is_error()) {
      ADD_FAILURE() << result.error();
    }
    loop_.Quit();
    return result;
  });

  executor.schedule_task(std::move(new_promise));

  zx_status_t status = loop_.Run();
  ASSERT_EQ(status, ZX_ERR_CANCELED);
}

IntegrationTest::Promise<void> IntegrationTest::CreateFirstChild(
    std::unique_ptr<RootMockDevice>* root_mock_device, std::unique_ptr<MockDevice>* child_device) {
  return ExpectBind(root_mock_device, [this, root_mock_device, child_device](
                                          HookInvocation record, Completer<void> completer) {
    ActionList actions;
    actions.AppendAddMockDevice(loop_.dispatcher(), (*root_mock_device)->path(), "first_child",
                                std::vector<zx_device_prop_t>{}, ZX_OK, std::move(completer),
                                child_device);
    actions.AppendReturnStatus(ZX_OK);
    return actions;
  });
}

IntegrationTest::Promise<void> IntegrationTest::ExpectUnbindThenRelease(
    const std::unique_ptr<MockDevice>& device) {
  fit::bridge<void, Error> bridge;
  auto unbind = ExpectUnbind(device, [unbind_reply_completer = std::move(bridge.completer)](
                                         HookInvocation record, Completer<void> completer) mutable {
    completer.complete_ok();
    ActionList actions;
    actions.AppendUnbindReply(std::move(unbind_reply_completer));
    return actions;
  });
  auto reply_done = bridge.consumer.promise_or(::fit::error("unbind_reply_completer abandoned"));
  return unbind.and_then(JoinPromises(std::move(reply_done), ExpectRelease(device)));
}

IntegrationTest::Promise<void> IntegrationTest::ExpectBind(
    std::unique_ptr<RootMockDevice>* root_mock_device, BindOnce::Callback actions_callback) {
  fit::bridge<void, Error> bridge;
  auto bind_hook =
      std::make_unique<BindOnce>(std::move(bridge.completer), std::move(actions_callback));
  zx_status_t status =
      RootMockDevice::Create(devmgr_, loop_.dispatcher(), std::move(bind_hook), root_mock_device);
  PROMISE_ASSERT(ASSERT_EQ(status, ZX_OK));
  return bridge.consumer.promise_or(::fit::error("bind abandoned"));
}

IntegrationTest::Promise<void> IntegrationTest::ExpectUnbind(
    const std::unique_ptr<MockDevice>& device, UnbindOnce::Callback actions_callback) {
  fit::bridge<void, Error> bridge;
  auto unbind_hook =
      std::make_unique<UnbindOnce>(std::move(bridge.completer), std::move(actions_callback));
  // Wrap the body in a promise, since we want to defer the evaluation of
  // device->set_hooks.
  return fit::make_promise([consumer = std::move(bridge.consumer), &device,
                            unbind_hook = std::move(unbind_hook)]() mutable {
    device->set_hooks(std::move(unbind_hook));
    return consumer.promise_or(::fit::error("unbind abandoned"));
  });
}

IntegrationTest::Promise<void> IntegrationTest::ExpectOpen(
    const std::unique_ptr<MockDevice>& device, OpenOnce::Callback actions_callback) {
  fit::bridge<void, Error> bridge;
  auto open_hook =
      std::make_unique<OpenOnce>(std::move(bridge.completer), std::move(actions_callback));
  // Wrap the body in a promise, since we want to defer the evaluation of
  // device->set_hooks.
  return fit::make_promise(
      [consumer = std::move(bridge.consumer), &device, open_hook = std::move(open_hook)]() mutable {
        device->set_hooks(std::move(open_hook));
        return consumer.promise_or(::fit::error("open abandoned"));
      });
}

IntegrationTest::Promise<void> IntegrationTest::ExpectClose(
    const std::unique_ptr<MockDevice>& device, CloseOnce::Callback actions_callback) {
  fit::bridge<void, Error> bridge;
  auto close_hook =
      std::make_unique<CloseOnce>(std::move(bridge.completer), std::move(actions_callback));
  // Wrap the body in a promise, since we want to defer the evaluation of
  // device->set_hooks.
  return fit::make_promise([consumer = std::move(bridge.consumer), &device,
                            close_hook = std::move(close_hook)]() mutable {
    device->set_hooks(std::move(close_hook));
    return consumer.promise_or(::fit::error("close abandoned"));
  });
}

IntegrationTest::Promise<void> IntegrationTest::ExpectRelease(
    const std::unique_ptr<MockDevice>& device) {
  // Wrap the body in a promise, since we want to defer the evaluation of
  // device->set_hooks.
  return fit::make_promise([&device]() {
    fit::bridge<void, Error> bridge;
    ReleaseOnce::Callback func = [](HookInvocation record, Completer<void> completer) {
      completer.complete_ok();
    };
    auto release_hook = std::make_unique<ReleaseOnce>(std::move(bridge.completer), std::move(func));
    device->set_hooks(std::move(release_hook));
    return bridge.consumer.promise_or(::fit::error("release abandoned"));
  });
}

IntegrationTest::Promise<void> IntegrationTest::DoOpen(
    const std::string& path, fidl::InterfacePtr<fuchsia::io::Node>* client) {
  fidl::InterfaceRequest<fuchsia::io::Node> server(client->NewRequest(loop_.dispatcher()));
  PROMISE_ASSERT(ASSERT_TRUE(server.is_valid()));

  PROMISE_ASSERT(ASSERT_EQ(client->events().OnOpen, nullptr));
  fit::bridge<void, Error> bridge;
  client->events().OnOpen = [client, completer = std::move(bridge.completer)](
                                zx_status_t status,
                                std::unique_ptr<fuchsia::io::NodeInfo> info) mutable {
    if (status != ZX_OK) {
      std::string error("failed to open node: ");
      error.append(zx_status_get_string(status));
      completer.complete_error(std::move(error));
      client->events().OnOpen = nullptr;
      return;
    }
    completer.complete_ok();
    client->events().OnOpen = nullptr;
  };
  devfs_->Open(fuchsia::io::OPEN_FLAG_DESCRIBE, 0, path, std::move(server));
  return bridge.consumer.promise_or(::fit::error("devfs open abandoned"));
}

namespace {

class AsyncWatcher {
 public:
  AsyncWatcher(std::string path, zx::channel watcher, fidl::InterfacePtr<fuchsia::io::Node> node)
      : path_(std::move(path)),
        watcher_(std::move(watcher)),
        connections_{std::move(node), {}},
        wait_(watcher_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0,
              fit::bind_member(this, &AsyncWatcher::WatcherChanged)) {}

  void WatcherChanged(async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                      const zx_packet_signal_t* signal);

  zx_status_t Begin(async_dispatcher_t* dispatcher,
                    fit::completer<void, IntegrationTest::Error> completer) {
    completer_ = std::move(completer);
    return wait_.Begin(dispatcher);
  }

  // Directory handle to keep alive for the lifetime of the AsyncWatcher, if
  // necessary.
  struct Connections {
    fidl::InterfacePtr<fuchsia::io::Node> node;
    fidl::InterfacePtr<fuchsia::io::Directory> directory;
  };

  Connections& connections() { return connections_; }

 private:
  std::string path_;
  zx::channel watcher_;
  Connections connections_;

  async::Wait wait_;
  fit::completer<void, IntegrationTest::Error> completer_;
};

void AsyncWatcher::WatcherChanged(async_dispatcher_t* dispatcher, async::Wait* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal) {
  auto error = [&](const char* msg) {
    completer_.complete_error(msg);
    delete this;
  };
  if (status != ZX_OK) {
    return error("watcher error");
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    char buf[fuchsia::io::MAX_FILENAME + 2 + 1];
    uint32_t bytes_read;
    status = watcher_.read(0, buf, nullptr, sizeof(buf) - 1, 0, &bytes_read, nullptr);
    if (status != ZX_OK) {
      return error("watcher read error");
    }
    buf[bytes_read] = 0;
    const char* filename = buf + 2;

    if (!strcmp(path_.c_str(), filename)) {
      completer_.complete_ok();
      delete this;
      return;
    }

    wait->Begin(dispatcher);
  } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    return error("watcher closed");
  }
}

void WaitForPath(const fidl::InterfacePtr<fuchsia::io::Directory>& dir,
                 async_dispatcher_t* dispatcher, std::string path,
                 fit::completer<void, IntegrationTest::Error> completer) {
  zx::channel watcher, remote;
  ASSERT_EQ(zx::channel::create(0, &watcher, &remote), ZX_OK);

  fidl::InterfacePtr<fuchsia::io::Node> last_dir;
  std::string filename;

  size_t last_slash = path.find_last_of('/');
  if (last_slash != std::string::npos) {
    std::string prefix(path, 0, last_slash);

    dir->Open(fuchsia::io::OPEN_FLAG_DIRECTORY | fuchsia::io::OPEN_FLAG_DESCRIBE |
                  fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
              0, prefix, last_dir.NewRequest(dispatcher));
    filename = path.substr(last_slash + 1);
  } else {
    dir->Clone(fuchsia::io::CLONE_FLAG_SAME_RIGHTS | fuchsia::io::OPEN_FLAG_DESCRIBE,
               last_dir.NewRequest(dispatcher));
    filename = path;
  }

  auto async_watcher =
      std::make_unique<AsyncWatcher>(std::move(filename), std::move(watcher), std::move(last_dir));
  auto& events = async_watcher->connections().node.events();
  events.OnOpen = [dispatcher, async_watcher = std::move(async_watcher),
                   completer = std::move(completer), remote = std::move(remote)](
                      zx_status_t status, std::unique_ptr<fuchsia::io::NodeInfo> info) mutable {
    if (status != ZX_OK) {
      completer.complete_error("Failed to open directory");
      return;
    }

    auto& dir = async_watcher->connections().directory;
    dir.Bind(async_watcher->connections().node.Unbind().TakeChannel(), dispatcher);

    dir->Watch(fuchsia::io::WATCH_MASK_ADDED | fuchsia::io::WATCH_MASK_EXISTING, 0,
               std::move(remote),
               [dispatcher, async_watcher = std::move(async_watcher),
                completer = std::move(completer)](zx_status_t status) mutable {
                 if (status == ZX_OK) {
                   status = async_watcher->Begin(dispatcher, std::move(completer));
                   if (status == ZX_OK) {
                     // The async_watcher will clean this up
                     __UNUSED auto ptr = async_watcher.release();
                     return;
                   }
                 }
                 completer.complete_error("watcher failed");
               });
  };
}

}  // namespace

IntegrationTest::Promise<void> IntegrationTest::DoWaitForPath(const std::string& path) {
  fit::bridge<void, Error> bridge;
  WaitForPath(devfs_, loop_.dispatcher(), path, std::move(bridge.completer));
  return bridge.consumer.promise_or(::fit::error("WaitForPath abandoned"));
}

}  // namespace libdriver_integration_test
