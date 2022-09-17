// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <thread>

/***
 * vtc-test
 *
 * Manual validation for Vulkan Traceable Component behavior when parented to actual
 * /core/agis:vulkan-trace.  Vtcs, in production, can only access the ComponentRegistry
 * protocol from Agis.
 *
 * fx ffx component run --recreate /core/agis/vulkan-trace:vtc-test
 *     fuchsia-pkg://fuchsia.com/vtc-test#meta/vtc-test.cm
 *
 ***/

namespace {
std::atomic<int> outstanding = 0;

zx_koid_t ProcessKoid() {
  const zx::unowned<zx::process> process = zx::process::self();
  zx_info_handle_basic_t info;
  const zx_status_t status =
      zx_object_get_info(process->get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                         nullptr /* actual */, nullptr /* avail */);
  ZX_ASSERT(status == ZX_OK);
  return info.koid;
}

std::string ProcessName() {
  const zx::unowned<zx::process> process = zx::process::self();
  char process_name[ZX_MAX_NAME_LEN];
  process->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  return process_name;
}

uint64_t TimeMS() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

class VtcTest {
 public:
  // Register, retrieve vulkan socket, listen for commands and log them to the console.
  void Communicate(uint32_t wait_secs = 35);

  void SetUp() {
    num_vtcs_ = 0;
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

    const zx_status_t status =
        context->svc()->Connect(component_registry_.NewRequest(loop_->dispatcher()));
    ZX_ASSERT(status == ZX_OK);
    component_registry_.set_error_handler([this](zx_status_t status) {
      FX_SLOG(ERROR, "|component_registry_| error handler", KV("status", status));
      loop_->Quit();
      assert(false);
    });

    process_koid_ = ProcessKoid();
    process_name_ = ProcessName();
    client_id_ = TimeMS();
  }

  void TearDown() {
    if (loop_) {
      LoopWait();
      loop_->Quit();
    }
  }

  // LoopWait() guarantees that the callback supplied to any interface in the agis protocol
  // library has been called and completed.  The callbacks compute / verify return results
  // from the protocol methods.
  void LoopWait() const {
    while (outstanding) {
      loop_->RunUntilIdle();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  void Register(uint64_t id, zx_koid_t process_koid, std::string process_name) {
    outstanding++;
    component_registry_->Register(
        id, process_koid, std::move(process_name),
        [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
          ZX_ASSERT(!result.err());
          outstanding--;
        });
    LoopWait();
  }

  void Unregister(uint64_t client_id) const {
    outstanding++;
    component_registry_->Unregister(
        client_id, [&](fuchsia::gpu::agis::ComponentRegistry_Unregister_Result result) {
          ZX_ASSERT(!result.err());
          outstanding--;
        });
  }

  // Ivars.
  std::unique_ptr<async::Loop> loop_;
  fuchsia::gpu::agis::ComponentRegistryPtr component_registry_;
  size_t num_vtcs_;
  zx_koid_t process_koid_;
  std::string process_name_;
  uint64_t client_id_;
};

void VtcTest::Communicate(uint32_t wait_secs) {
  printf("VtcTest::Communicate()\n");
  Register(client_id_, process_koid_, process_name_);

  zx::socket vulkan_socket;
  component_registry_->GetVulkanSocket(
      client_id_, [&](fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Result result) {
        fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Response response(
            std::move(result.response()));
        ZX_ASSERT(!result.err());
        vulkan_socket = response.ResultValue_();
        ZX_ASSERT(vulkan_socket.is_valid());
        printf("VtcTest::Communicate: vulkan socket established %d\n", vulkan_socket.is_valid());
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  loop_->RunUntilIdle();

  // |vulkan_socket| will resolve when `ffx agis listen <global_id>` is called.
  ZX_ASSERT(!vulkan_socket.is_valid());

  printf("(1) VtcTest::Communicate: Retrieve global_id from `ffx agis vtcs`\n");
  printf(
      "(2) VtcTest::Communicate: Awaiting `ffx agis listen <global_id>` invocation to resolve "
      "vulkan socket for %d secs...\n",
      wait_secs);
  fflush(stdout);

  // Allow time to run `ffx agis listen`.
  std::this_thread::sleep_for(std::chrono::seconds(wait_secs));
  loop_->RunUntilIdle();

  // |vulkan_socket| should be valid after `ffx agis listen`.
  ZX_ASSERT(vulkan_socket.is_valid());

  // External step write something to the unix socket.
  printf(
      "(3) External step: now write something to the pipe `/tmp/agis<global_id>`, sleeping %d "
      "seconds ...\n",
      wait_secs);
  fflush(stdout);
  std::this_thread::sleep_for(std::chrono::seconds(wait_secs));

  // Read something from the unix socket.
  char buf[128];
  size_t actual = 0;
  const zx_status_t status = vulkan_socket.read(0u, buf, sizeof(buf), &actual);
  ZX_ASSERT(status == ZX_OK);
  printf("Read %zu bytes from |vulkan_socket|.\n", actual);
  fflush(stdout);

  // Remove registration.
  Unregister(client_id_);
}

int main(int argc, char **argv) {
  VtcTest test;
  syslog::SetTags({"vtc-test"});

  test.SetUp();

  if (argc == 2) {
    const uint32_t wait_secs = atoi(argv[1]);
    test.Communicate(wait_secs);
  } else {
    test.Communicate();
  }

  test.TearDown();

  return 0;
}
