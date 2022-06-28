// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>
#include <lib/zxio/zxio.h>

#include <thread>

#include <gtest/gtest.h>
#include <sdk/lib/zxio/include/lib/zxio/zxio.h>
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

// AgisTest uses asynchronous FIDL entrypoints to provide reusable code for real
// world usage of the agis service.
class AgisTest : public testing::Test {
 protected:
  void SetUp() override {
    num_vtcs_ = 0;
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

    context->svc()->Connect(component_registry_.NewRequest(loop_->dispatcher()));
    component_registry_.set_error_handler([this](zx_status_t status) {
      FX_LOG(ERROR, "agis-test", "AgisTest::ErrHandler ComponentRegistry");
      loop_->Quit();
    });

    context->svc()->Connect(observer_.NewRequest(loop_->dispatcher()));
    observer_.set_error_handler([this](zx_status_t status) {
      FX_LOG(ERROR, "agis-test", "AgisTest::ErrHandler Observer");
      loop_->Quit();
    });

    process_koid_ = ProcessKoid();
    process_name_ = ProcessName();
    time_ms_ = TimeMS();
  }

  void TearDown() override {
    if (loop_) {
      LoopWait();
      loop_->Quit();
    }
  }

  void LoopWait() {
    while (outstanding) {
      EXPECT_EQ(loop_->RunUntilIdle(), ZX_OK);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  void Register(uint64_t id, zx_koid_t process_koid, std::string process_name) {
    outstanding++;
    component_registry_->Register(
        id, process_koid, std::move(process_name),
        [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
          fuchsia::gpu::agis::ComponentRegistry_Register_Response response(
              std::move(result.response()));
          EXPECT_FALSE(result.err());
          zx::socket gapii_socket = response.ResultValue_();
          EXPECT_NE(gapii_socket, ZX_HANDLE_INVALID);
          outstanding--;
        });
  }

  void Unregister(uint64_t id) {
    outstanding++;
    component_registry_->Unregister(
        id, [&](fuchsia::gpu::agis::ComponentRegistry_Unregister_Result result) {
          EXPECT_FALSE(result.err());
          outstanding--;
        });
  }

  void Vtcs() {
    outstanding++;
    observer_->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
      fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
      std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
      for (auto &vtc : vtcs) {
        FX_LOGF(INFO, "agis-test", "AgisTest::Vtc \"%lu\" \"%s\"", vtc.process_koid(),
                vtc.process_name().c_str());
      }
      num_vtcs_ = vtcs.size();
      outstanding--;
    });
  }

  std::unique_ptr<async::Loop> loop_;
  fuchsia::gpu::agis::ComponentRegistryPtr component_registry_;
  fuchsia::gpu::agis::ObserverPtr observer_;
  size_t num_vtcs_;
  zx_koid_t process_koid_;
  std::string process_name_;

  // |time_ms| is used as the unique vtc ID throughout all tests.
  uint64_t time_ms_;
};

TEST_F(AgisTest, Register) {
  Register(time_ms_, process_koid_, process_name_);
  outstanding++;
  component_registry_->Register(time_ms_, process_koid_, process_name_,
                                [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                  EXPECT_EQ(result.err(),
                                            fuchsia::gpu::agis::Error::ALREADY_REGISTERED);
                                  outstanding--;
                                });
  Unregister(time_ms_);
}

TEST_F(AgisTest, Unregister) {
  Register(time_ms_, process_koid_, process_name_);
  Unregister(time_ms_);
  component_registry_->Unregister(
      time_ms_, [&](fuchsia::gpu::agis::ComponentRegistry_Unregister_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(result.err(), fuchsia::gpu::agis::Error::NOT_FOUND);
      });
}

TEST_F(AgisTest, Vtcs) {
  Register(time_ms_, process_koid_, process_name_);
  Register(time_ms_ + 1, process_koid_, process_name_ + "+1");
  LoopWait();
  Vtcs();
  LoopWait();
  EXPECT_EQ(num_vtcs_, 2ul);
  Unregister(time_ms_);
  Unregister(time_ms_ + 1);
  LoopWait();
  Vtcs();
  LoopWait();
  EXPECT_EQ(num_vtcs_, 0ul);
}

TEST_F(AgisTest, MaxVtcs) {
  uint32_t i = 0;
  for (i = 0; i < fuchsia::gpu::agis::MAX_VTCS; i++) {
    Register(time_ms_ + i, process_koid_, process_name_ + "+" + std::to_string(i));
  }
  outstanding++;
  component_registry_->Register(time_ms_ + i, process_koid_,
                                process_name_ + "+" + std::to_string(i),
                                [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                  EXPECT_EQ(result.err(), fuchsia::gpu::agis::Error::VTCS_EXCEEDED);
                                  outstanding--;
                                });
  for (i = 0; i < fuchsia::gpu::agis::MAX_VTCS; i++) {
    Unregister(time_ms_ + i);
  }
}

TEST_F(AgisTest, UsableSocket) {
  // Register and retrieve the socket for the server.
  outstanding++;
  zx::socket gapii_socket;
  component_registry_->Register(time_ms_, process_koid_, process_name_,
                                [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                  fuchsia::gpu::agis::ComponentRegistry_Register_Response response(
                                      std::move(result.response()));
                                  EXPECT_FALSE(result.err());
                                  gapii_socket = response.ResultValue_();
                                  outstanding--;
                                });
  LoopWait();
  EXPECT_NE(gapii_socket, ZX_HANDLE_INVALID);

  // Get the socket endpoint assignment for the client.
  outstanding++;
  zx::socket agi_socket;
  std::string process_name;
  zx_koid_t process_koid;
  observer_->Vtcs([&](fuchsia::gpu::agis::Observer_Vtcs_Result result) {
    fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(result.response()));
    EXPECT_FALSE(result.err());
    std::vector<fuchsia::gpu::agis::Vtc> vtcs(response.ResultValue_());
    EXPECT_EQ(vtcs.size(), 1ul);
    vtcs.front().agi_socket().duplicate(ZX_RIGHT_SAME_RIGHTS, &agi_socket);
    process_name = vtcs.front().process_name();
    process_koid = vtcs.front().process_koid();
    outstanding--;
  });
  LoopWait();
  EXPECT_NE(agi_socket, ZX_HANDLE_INVALID);
  EXPECT_EQ(process_name, process_name_);
  EXPECT_EQ(process_koid, process_koid_);

  // Send message from gapii end.
  const char message[] = "AGIS Server Message";
  size_t actual = 0;
  zx_status_t status = gapii_socket.write(0u, message, sizeof(message), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));

  // Read message from agi end.
  char buffer[sizeof(message)];
  actual = 0;
  status = agi_socket.read(0u, &buffer, sizeof(buffer), &actual);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(actual, sizeof(message));
  EXPECT_EQ(strcmp(message, buffer), 0);
}

TEST(AgisDisconnect, Main) {
  zx_koid_t process_koid = ProcessKoid();
  std::string process_name = ProcessName();
  uint64_t time_ms = TimeMS();
  bool disconnect_outstanding = false;
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();
  auto loop_wait = [&disconnect_outstanding, &loop]() {
    while (disconnect_outstanding) {
      EXPECT_EQ(loop->RunUntilIdle(), ZX_OK);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  };

  // Create a component_registry, register |time_ms| as the ID and verify its presence.
  {
    fuchsia::gpu::agis::ComponentRegistryPtr component_registry;
    context->svc()->Connect(component_registry.NewRequest(loop->dispatcher()));
    component_registry.set_error_handler([&loop](zx_status_t status) {
      FX_LOGF(ERROR, "agis-test", "Register Disconnect ComponentRegistry ErrHandler - status %d",
              status);
      if (loop) {
        loop->Quit();
      }
    });

    disconnect_outstanding = true;
    component_registry->Register(time_ms, process_koid, process_name,
                                 [&](fuchsia::gpu::agis::ComponentRegistry_Register_Result result) {
                                   EXPECT_FALSE(result.err());
                                   disconnect_outstanding = false;
                                 });
    loop_wait();

    fuchsia::gpu::agis::ObserverPtr observer;
    context->svc()->Connect(observer.NewRequest(loop->dispatcher()));
    observer.set_error_handler([&loop](zx_status_t status) {
      FX_LOGF(ERROR, "agis-test", "Register Disconnect Observer ErrHandler - status %d", status);
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
    FX_LOGF(ERROR, "agis-test", "Verify Disconnect Observer ErrHandler - status %d", status);
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
  return RUN_ALL_TESTS();
}
