// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "integration-test.h"

#include <fcntl.h>

#include <lib/async_promise/executor.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fit/bridge.h>
#include <zircon/status.h>

namespace libdriver_integration_test {

IntegrationTest::IsolatedDevmgr IntegrationTest::devmgr_;
const zx::duration IntegrationTest::kDefaultTimeout = zx::sec(5);

void IntegrationTest::SetUpTestCase() {
    // Set up the isolated devmgr instance for this test suite.  Note that we
    // only do this once for the whole suite, because it is currently an
    // expensive process.  Ideally we'd do this between every test.
    auto args = IsolatedDevmgr::DefaultArgs();
    args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));

    zx_status_t status = IsolatedDevmgr::Create(std::move(args), &IntegrationTest::devmgr_);
    if (status != ZX_OK) {
        printf("libdriver-integration-tests: failed to create isolated devmgr\n");
        return;
    }
}

void IntegrationTest::TearDownTestCase() {
    IntegrationTest::devmgr_.reset();
}

IntegrationTest::IntegrationTest()
    : loop_(&kAsyncLoopConfigNoAttachToThread),
      devmgr_exception_(this, devmgr_.containing_job().get(), 0) {

    zx_status_t status = devmgr_exception_.Bind(loop_.dispatcher());
    if (status != ZX_OK) {
        printf("libdriver-integration-tests: failed to watch isolated devmgr for crashes: %s\n",
               zx_status_get_string(status));
        return;
    }

    fdio_t* io = fdio_unsafe_fd_to_io(IntegrationTest::devmgr_.devfs_root().get());
    status = devfs_.Bind(zx::channel(fdio_service_clone(fdio_unsafe_borrow_channel(io))),
                         loop_.dispatcher());
    fdio_unsafe_release(io);
    if (status != ZX_OK) {
        printf("libdriver-integration-tests: failed to connect to devfs\n");
        return;
    }
}

IntegrationTest::~IntegrationTest() = default;

void IntegrationTest::DevmgrException(async_dispatcher_t* dispatcher,
                                      async::ExceptionBase* exception, zx_status_t status,
                                      const zx_port_packet_t* report) {
    // Log an error in the currently running test
    ADD_FAILURE() << "Crash inside devmgr job";
    exception->Unbind();
    loop_.Quit();
}

void IntegrationTest::RunPromise(Promise<void> promise) {
    RunPromise(std::move(promise), zx::deadline_after(kDefaultTimeout));
}

void IntegrationTest::RunPromise(Promise<void> promise, zx::time deadline) {
    async::Executor executor(loop_.dispatcher());

    auto new_promise = promise.then([&](Promise<void>::result_type& result) {
        if (result.is_error()) {
            ADD_FAILURE() << result.error();
        }
        loop_.Quit();
        return result;
    });

    executor.schedule_task(std::move(new_promise));

    zx_status_t status = loop_.Run(deadline);
    ASSERT_EQ(status, ZX_ERR_CANCELED);
}

IntegrationTest::Promise<void> IntegrationTest::CreateFirstChild(
        std::unique_ptr<RootMockDevice>* root_mock_device,
        std::unique_ptr<MockDevice>* child_device) {
    return ExpectBind(root_mock_device,
        [this, root_mock_device, child_device](HookInvocation record,
                                               Completer<void> completer) {
            ActionList actions;
            actions.AppendAddMockDevice(loop_.dispatcher(), (*root_mock_device)->path(),
                                        "first_child", std::vector<zx_device_prop_t>{}, ZX_OK,
                                        std::move(completer), child_device);
            actions.AppendReturnStatus(ZX_OK);
            return actions;
        });
}

IntegrationTest::Promise<void> IntegrationTest::ExpectUnbindThenRelease(
        const std::unique_ptr<MockDevice>& device) {
    fit::bridge<void, Error> bridge;
    auto unbind = ExpectUnbind(device,
        [remove_completer = std::move(bridge.completer)](HookInvocation record,
                                                         Completer<void> completer) mutable {
            completer.complete_ok();
            ActionList actions;
            actions.AppendRemoveDevice(std::move(remove_completer));
            return actions;
        });
    auto remove_done = bridge.consumer.promise_or(::fit::error("remove_completer abandoned"));
    return unbind.and_then(JoinPromises(std::move(remove_done), ExpectRelease(device)));
}

IntegrationTest::Promise<void> IntegrationTest::ExpectBind(
        std::unique_ptr<RootMockDevice>* root_mock_device, BindOnce::Callback actions_callback) {
    fit::bridge<void, Error> bridge;
    auto bind_hook = std::make_unique<BindOnce>(std::move(bridge.completer),
                                                std::move(actions_callback));
    zx_status_t status = RootMockDevice::Create(devmgr_, loop_.dispatcher(),
                                                std::move(bind_hook), root_mock_device);
    PROMISE_ASSERT(ASSERT_EQ(status, ZX_OK));
    return bridge.consumer.promise_or(::fit::error("bind abandoned"));
}

IntegrationTest::Promise<void> IntegrationTest::ExpectUnbind(
        const std::unique_ptr<MockDevice>& device, UnbindOnce::Callback actions_callback) {
    fit::bridge<void, Error> bridge;
    auto unbind_hook = std::make_unique<UnbindOnce>(
            std::move(bridge.completer), std::move(actions_callback));
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
    auto open_hook = std::make_unique<OpenOnce>(
            std::move(bridge.completer), std::move(actions_callback));
    // Wrap the body in a promise, since we want to defer the evaluation of
    // device->set_hooks.
    return fit::make_promise([consumer = std::move(bridge.consumer), &device,
                             open_hook = std::move(open_hook)]() mutable {
                                 device->set_hooks(std::move(open_hook));
                                 return consumer.promise_or(::fit::error("open abandoned"));
                             });
}

IntegrationTest::Promise<void> IntegrationTest::ExpectClose(
        const std::unique_ptr<MockDevice>& device, CloseOnce::Callback actions_callback) {
    fit::bridge<void, Error> bridge;
    auto close_hook = std::make_unique<CloseOnce>(
            std::move(bridge.completer), std::move(actions_callback));
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
        auto release_hook = std::make_unique<ReleaseOnce>(std::move(bridge.completer),
                                                          std::move(func));
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
            zx_status_t status, std::unique_ptr<fuchsia::io::NodeInfo> info) mutable {
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

} // namespace libdriver_integration_test
