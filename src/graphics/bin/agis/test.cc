// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zxio/zxio.h>

#include <thread>

#include <gtest/gtest.h>
namespace {
std::atomic<int> outstanding = 0;

zx_koid_t ProcessKoid() {
  zx::unowned<zx::process> process = zx::process::self();
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(process->get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                          nullptr /* actual */, nullptr /* avail */);
  EXPECT_EQ(status, ZX_OK);
  return info.koid;
}

std::string ProcessName() {
  zx::unowned<zx::process> process = zx::process::self();
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

class AgisTest : public testing::Test {
 protected:
  void SetUp() override {
    num_vtcs_ = 0;
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

    zx_status_t status =
        context->svc()->Connect(component_registry_.NewRequest(loop_->dispatcher()));
    EXPECT_EQ(status, ZX_OK);
    component_registry_.set_error_handler([this](zx_status_t status) {
      FX_SLOG(ERROR, "|component_registry_| error handler", KV("status", status));
      loop_->Quit();
      ASSERT_TRUE(false);
    });

    status = context->svc()->Connect(observer_.NewRequest(loop_->dispatcher()));
    EXPECT_EQ(status, ZX_OK);
    observer_.set_error_handler([this](zx_status_t status) {
      FX_SLOG(ERROR, "agis-test: |observer_| error handler");
      loop_->Quit();
      ASSERT_TRUE(false);
    });

    status = context->svc()->Connect(connector_.NewRequest(loop_->dispatcher()));
    EXPECT_EQ(status, ZX_OK);
    connector_.set_error_handler([this](zx_status_t status) {
      FX_SLOG(ERROR, "agis-test: |connector_| error handler");
      loop_->Quit();
      ASSERT_TRUE(false);
    });

    process_koid_ = ProcessKoid();
    process_name_ = ProcessName();
    client_id_ = TimeMS();
  }

  void TearDown() override {
    if (loop_) {
      LoopWait();
      loop_->Quit();
    }
  }

  // LoopWait() guarantees that the callback supplied to any interface in the agis protocol
  // library has been called and completed.  The callbacks compute / verify return results
  // from the protocol methods.
  void LoopWait(const uint64_t timeout_secs = 0) {
    uint64_t elapsed_millis = 0;
    const uint64_t timeout_millis = timeout_secs * 1000;
    while (outstanding && (timeout_secs == 0 || elapsed_millis < timeout_millis)) {
      loop_->RunUntilIdle();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      elapsed_millis += 50;
    }
  }

  void Register(uint64_t id, zx_koid_t process_koid, std::string process_name) {
    outstanding++;
    component_registry_->Register(
        id, process_koid, std::move(process_name),
        [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
          EXPECT_FALSE(result.err());
          outstanding--;
        });
    LoopWait();
  }

  void Unregister(uint64_t client_id) {
    outstanding++;
    component_registry_->Unregister(
        client_id, [&](fuchsia::gpu::agis::ComponentRegistry_Unregister_Result result) {
          EXPECT_FALSE(result.err());
          outstanding--;
        });
  }

  void Vtcs() {
    outstanding++;
    observer_->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
      fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
      EXPECT_FALSE(result.err());
      std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
      num_vtcs_ = vtcs.size();
      outstanding--;
    });
  }

  //
  // Ivars.
  //
  std::unique_ptr<async::Loop> loop_;
  fuchsia::gpu::agis::ComponentRegistryPtr component_registry_;
  fuchsia::gpu::agis::ObserverPtr observer_;
  fuchsia::gpu::agis::ConnectorPtr connector_;
  size_t num_vtcs_;
  zx_koid_t process_koid_;
  std::string process_name_;
  uint64_t client_id_;
};

// Test register.
TEST_F(AgisTest, Register) {
  Register(client_id_, process_koid_, process_name_);
  outstanding++;
  component_registry_->Register(client_id_, process_koid_, process_name_,
                                [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                  EXPECT_EQ(result.err(),
                                            fuchsia::gpu::agis::Error::ALREADY_REGISTERED);
                                  outstanding--;
                                });
  LoopWait();
  Unregister(client_id_);
  LoopWait();
}

// Test unregister.
TEST_F(AgisTest, Unregister) {
  Register(client_id_, process_koid_, process_name_);
  Unregister(client_id_);
  LoopWait();
  outstanding++;
  component_registry_->Unregister(
      client_id_, [&](fuchsia::gpu::agis::ComponentRegistry_Unregister_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.err(), fuchsia::gpu::agis::Error::NOT_FOUND);
        outstanding--;
      });
  LoopWait();
}

// Test vtc list cardinality.
TEST_F(AgisTest, Vtcs) {
  Register(client_id_, process_koid_, process_name_);
  Register(client_id_ + 1, process_koid_, process_name_ + "+1");
  Vtcs();
  LoopWait();
  EXPECT_EQ(num_vtcs_, 2ul);
  Unregister(client_id_);
  Unregister(client_id_ + 1);
  LoopWait();
  Vtcs();
  LoopWait();
  EXPECT_EQ(num_vtcs_, 0ul);
}

// Test registry overflow.
TEST_F(AgisTest, MaxVtcs) {
  uint32_t i = 0;
  for (i = 0; i < fuchsia::gpu::agis::MAX_VTCS; i++) {
    Register(client_id_ + i, process_koid_, process_name_ + "+" + std::to_string(i));
  }
  outstanding++;
  component_registry_->Register(client_id_ + i, process_koid_,
                                process_name_ + "+" + std::to_string(i),
                                [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                  EXPECT_EQ(result.err(), fuchsia::gpu::agis::Error::VTCS_EXCEEDED);
                                  outstanding--;
                                });
  LoopWait();
  for (i = 0; i < fuchsia::gpu::agis::MAX_VTCS; i++) {
    Unregister(client_id_ + i);
  }
  LoopWait();
}

// Validate retrieved socket.
TEST_F(AgisTest, UsableSocket) {
  outstanding++;
  component_registry_->Register(client_id_, process_koid_, process_name_,
                                [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                  EXPECT_FALSE(result.err());
                                  outstanding--;
                                });
  LoopWait();

  // Issue hanging get for the vulkan-end socket.  Notice there is no following LoopWait() call.
  zx::socket vulkan_socket;
  component_registry_->GetVulkanSocket(
      client_id_, [&](fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Result result) {
        fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Response response(
            std::move(result.response()));
        EXPECT_FALSE(result.err());
        vulkan_socket = response.ResultValue_();
        EXPECT_TRUE(vulkan_socket.is_valid());
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  loop_->RunUntilIdle();
  EXPECT_FALSE(vulkan_socket.is_valid());

  uint32_t global_id = 0;
  std::string process_name;
  zx_koid_t process_koid = 0;
  outstanding++;
  observer_->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
    fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
    EXPECT_EQ(vtcs.size(), 1ul);
    global_id = vtcs.front().global_id();
    process_koid = vtcs.front().process_koid();
    process_name = vtcs.front().process_name();
    outstanding--;
  });
  LoopWait();
  EXPECT_EQ(process_name, process_name_);
  EXPECT_EQ(process_koid, process_koid_);
  EXPECT_NE(global_id, 0u);

  // Explicitly retrieve the ffx-end socket and implicitly satisfy the hanging GetVulkanSocket.
  outstanding++;
  zx::socket ffx_socket;
  connector_->GetSocket(global_id, [&](fuchsia::gpu::agis::Connector_GetSocket_Result result) {
    fuchsia::gpu::agis::Connector_GetSocket_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    ffx_socket = response.ResultValue_();
    outstanding--;
  });

  while (!vulkan_socket.is_valid() || !ffx_socket.is_valid()) {
    LoopWait(30);
  }

  // Send the message from the vulkan end.
  const char message[] = "AGIS Server Message";
  size_t actual = 0;
  zx_status_t status = vulkan_socket.write(0u, message, sizeof(message), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));

  // Read the message from the ffx end.
  char buffer[sizeof(message)];
  actual = 0;
  status = ffx_socket.read(0u, &buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));
  EXPECT_EQ(strcmp(message, buffer), 0);
}

// Test multiple retrievals of the vulkan socket from the same registration.
TEST_F(AgisTest, Reget) {
  Register(client_id_, process_koid_, process_name_);

  // Issue hanging get for the vulkan-end socket.  Notice there is no following LoopWait() call.
  zx::socket vulkan_socket;
  component_registry_->GetVulkanSocket(
      client_id_, [&](fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Result result) {
        fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Response response(
            std::move(result.response()));
        EXPECT_FALSE(result.err());
        vulkan_socket = response.ResultValue_();
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  loop_->RunUntilIdle();
  EXPECT_FALSE(vulkan_socket.is_valid());

  outstanding++;
  uint32_t global_id = 0;
  std::string process_name;
  zx_koid_t process_koid = 0;
  observer_->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
    fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
    EXPECT_EQ(vtcs.size(), 1ul);
    global_id = vtcs.front().global_id();
    process_name = vtcs.front().process_name();
    process_koid = vtcs.front().process_koid();
    outstanding--;
  });
  LoopWait();

  // Explicitly retrieve the ffx-end socket and implicitly satisfy the hanging GetVulkanSocket.
  outstanding++;
  zx::socket ffx_socket;
  connector_->GetSocket(global_id, [&](fuchsia::gpu::agis::Connector_GetSocket_Result result) {
    fuchsia::gpu::agis::Connector_GetSocket_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    ffx_socket = response.ResultValue_();
    outstanding--;
  });

  while (!vulkan_socket.is_valid() || !ffx_socket.is_valid()) {
    LoopWait(30);
  }

  // Re-get the socket.
  vulkan_socket.reset();
  component_registry_->GetVulkanSocket(
      client_id_, [&](fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Result result) {
        fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Response response(
            std::move(result.response()));
        EXPECT_FALSE(result.err());
        vulkan_socket = response.ResultValue_();
      });

  outstanding++;
  connector_->GetSocket(global_id, [&](fuchsia::gpu::agis::Connector_GetSocket_Result result) {
    fuchsia::gpu::agis::Connector_GetSocket_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    ffx_socket = response.ResultValue_();
    outstanding--;
  });

  while (!vulkan_socket.is_valid() || !ffx_socket.is_valid()) {
    LoopWait(30);
  }

  // Send the message from the vulkan end.
  const char message[] = "AGIS Server Message";
  size_t actual = 0;
  auto status = vulkan_socket.write(0u, message, sizeof(message), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));

  // Read the message from the ffx end.
  char buffer[sizeof(message)];
  actual = 0;
  status = ffx_socket.read(0u, &buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));
  EXPECT_EQ(strcmp(message, buffer), 0);

  Unregister(client_id_);
  LoopWait();
}

// Test GetSocket followed by GetVulkanSocket retrieval.
TEST_F(AgisTest, ReverseGet) {
  Register(client_id_, process_koid_, process_name_);

  outstanding++;
  uint32_t global_id = 0;
  std::string process_name;
  zx_koid_t process_koid = 0;
  observer_->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
    fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
    EXPECT_EQ(vtcs.size(), 1ul);
    global_id = vtcs.front().global_id();
    process_name = vtcs.front().process_name();
    process_koid = vtcs.front().process_koid();
    outstanding--;
  });
  LoopWait();

  // Retrieve the ffx socket end first.
  outstanding++;
  zx::socket ffx_socket;
  connector_->GetSocket(global_id, [&](fuchsia::gpu::agis::Connector_GetSocket_Result result) {
    fuchsia::gpu::agis::Connector_GetSocket_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    ffx_socket = response.ResultValue_();
    outstanding--;
  });

  // Retrieve the vulkan socket end second.
  outstanding++;
  zx::socket vulkan_socket;
  component_registry_->GetVulkanSocket(
      client_id_, [&](fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Result result) {
        fuchsia::gpu::agis::ComponentRegistry_GetVulkanSocket_Response response(
            std::move(result.response()));
        EXPECT_FALSE(result.err());
        vulkan_socket = response.ResultValue_();
        outstanding--;
      });

  while (!vulkan_socket.is_valid() || !ffx_socket.is_valid()) {
    LoopWait(30);
  }

  // Send the message from the vulkan end.
  const char message[] = "AGIS Server Message";
  size_t actual = 0;
  auto status = vulkan_socket.write(0u, message, sizeof(message), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));

  // Read the message from the ffx end.
  char buffer[sizeof(message)];
  actual = 0;
  status = ffx_socket.read(0u, &buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));
  EXPECT_EQ(strcmp(message, buffer), 0);

  Unregister(client_id_);
  LoopWait();
}

// Test unexpected disconnects / shutdowns.
TEST(AgisDisconnect, Main) {
  zx_koid_t process_koid = ProcessKoid();
  std::string process_name = ProcessName();
  uint64_t client_id = TimeMS();
  bool disconnect_outstanding = false;
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();
  auto loop_wait = [&disconnect_outstanding, &loop]() {
    while (disconnect_outstanding) {
      EXPECT_EQ(loop->RunUntilIdle(), ZX_OK);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  };

  // Create a component_registry, register |client_id| and verify its presence.
  {
    fuchsia::gpu::agis::ComponentRegistryPtr component_registry;
    context->svc()->Connect(component_registry.NewRequest(loop->dispatcher()));
    component_registry.set_error_handler([&loop](zx_status_t status) {
      FX_SLOG(ERROR, "Register Disconnect ComponentRegistry ErrHandler", KV("status", status));
      if (loop) {
        loop->Quit();
      }
    });

    disconnect_outstanding = true;
    component_registry->Register(client_id, process_koid, process_name,
                                 [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                   EXPECT_FALSE(result.err());
                                   disconnect_outstanding = false;
                                 });
    loop_wait();

    fuchsia::gpu::agis::ObserverPtr observer;
    context->svc()->Connect(observer.NewRequest(loop->dispatcher()));
    observer.set_error_handler([&loop](zx_status_t status) {
      FX_SLOG(ERROR, "Register Disconnect Observer ErrHandler", KV("status", status));
      if (loop) {
        loop->Quit();
      }
    });
    disconnect_outstanding = true;
    observer->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
      fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
      EXPECT_FALSE(result.err());
      std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
      EXPECT_EQ(vtcs.size(), 1ul);
      bool found = false;
      for (const auto &vtc : vtcs) {
        if (vtc.process_koid() == process_koid) {
          EXPECT_EQ(vtc.process_name(), process_name);
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found);
      disconnect_outstanding = false;
    });
    loop_wait();
  }

  // Create a new observer and verify that |process_koid| is no longer registered.
  fuchsia::gpu::agis::ObserverPtr observer;
  context->svc()->Connect(observer.NewRequest(loop->dispatcher()));
  observer.set_error_handler([&loop](zx_status_t status) {
    FX_SLOG(ERROR, "Verify Disconnect Observer ErrHandler ", KV("status", status));
    if (loop) {
      loop->Quit();
    }
  });

  bool found = true;
  while (found) {
    disconnect_outstanding = true;
    observer->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
      EXPECT_FALSE(result.err());
      auto vtcs(result.response().ResultValue_());
      bool component_found = false;
      for (const auto &vtc : vtcs) {
        if (vtc.process_koid() == process_koid) {
          EXPECT_EQ(vtc.process_name(), process_name);
          component_found = true;
          break;
        }
      }
      found = component_found;
      disconnect_outstanding = false;
    });
    loop_wait();
  }

  // Self-documenting no-op.
  EXPECT_FALSE(found);

  loop->Quit();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  syslog::SetTags({"agis-test"});
  return RUN_ALL_TESTS();
}
