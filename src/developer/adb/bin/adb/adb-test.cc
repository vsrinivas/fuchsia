// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adb.h"

#include <fuchsia/hardware/adb/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sync/cpp/completion.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <memory>
#include <queue>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

namespace adb {

using namespace component_testing;

class FakeAdbDriver {
 public:
  explicit FakeAdbDriver() : device_(this) {}

  bool expectations_empty() {
    fbl::AutoLock _(&impl_.lock_);
    return device_.expect_start_ == 0 && impl_.expect_queue_tx_.empty() &&
           impl_.expect_receive_.empty();
  }

  // Helper functions to test ADB Protocol
  void ExpectStart() { device_.expect_start_++; }

  void ExpectQueueTx(uint32_t options, std::vector<uint8_t> buffer) {
    fbl::AutoLock _(&impl_.lock_);
    impl_.expect_queue_tx_.push(std::move(buffer));
  }
  void ExpectQueueTx(uint32_t options, apacket packet) {
    ExpectQueueTx(options, std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&packet),
                                                reinterpret_cast<uint8_t*>(&packet) +
                                                    sizeof(packet.msg) + packet.payload.size()));
  }

  void SendConnect() {
    apacket packet{
        .msg =
            {
                .command = A_CNXN,
                .arg0 = A_VERSION,
                .arg1 = MAX_PAYLOAD,
                .data_length = 234,
                .data_check = 0x5b44,
                .magic = A_CNXN ^ 0xffffffff,
            },
        .payload = {},
    };
    impl_.ExpectReceive(std::move(packet));
    std::string connection_string = "host::features=shell_v2,cmd,stat_v2,ls_v2,fixed_pu";
    std::vector<uint8_t> data = {connection_string.begin(), connection_string.end()};
    data.resize(234);
    impl_.ExpectReceive(std::move(data));
  }

  void SendOpen(std::vector<uint8_t> data) {
    impl_.ExpectReceive({.msg =
                             {
                                 .command = A_OPEN,
                                 .arg0 = 1,  // local-id:1
                                 .arg1 = 0,
                                 .data_length = static_cast<uint32_t>(data.size()),
                                 .data_check = 0,
                                 .magic = A_OPEN ^ 0xffffffff,
                             },
                         .payload = {data.begin(), data.end()}});
  }

  void SendWrite(std::vector<uint8_t> data) {
    impl_.ExpectReceive({.msg =
                             {
                                 .command = A_WRTE,
                                 .arg0 = 1,  // local-id:1
                                 .arg1 = 1,  // remote-id: 1
                                 .data_length = static_cast<uint32_t>(data.size()),
                                 .data_check = 0,
                                 .magic = A_WRTE ^ 0xffffffff,
                             },
                         .payload = {data.begin(), data.end()}});
  }

  void TearDown() {
    impl_.loop_.Shutdown();

    EXPECT_EQ(device_.expect_start_, 0UL);
    fbl::AutoLock _(&impl_.lock_);
    EXPECT_TRUE(impl_.expect_queue_tx_.empty());
    EXPECT_TRUE(impl_.expect_receive_.empty());
  }

  //  private:
  friend class DeviceServer;
  friend class UsbAdbImplServer;
  friend class TestDeviceConnector;

  class UsbAdbImplServer : public fidl::Server<fuchsia_hardware_adb::UsbAdbImpl> {
   public:
    UsbAdbImplServer() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
      EXPECT_EQ(loop_.StartThread("adb-test-impl-thread"), ZX_OK);
    }

    ~UsbAdbImplServer() override {
      if (receive_completer_.has_value()) {
        receive_completer_->Reply(fit::error(ZX_ERR_BAD_STATE));
        receive_completer_.reset();
        return;
      }
    }

    void Bind(fidl::ServerEnd<fuchsia_hardware_adb::UsbAdbImpl> server) {
      binding_.emplace(fidl::BindServer(loop_.dispatcher(), std::move(server), this));
    }

    // fuchsia_hardware_adb::UsbAdbImpl functions
    void QueueTx(QueueTxRequest& request, QueueTxCompleter::Sync& completer) override {
      std::vector<uint8_t> expect;
      {
        fbl::AutoLock _(&lock_);
        ASSERT_FALSE(expect_queue_tx_.empty());
        expect = std::move(expect_queue_tx_.front());
        expect_queue_tx_.pop();
      }
      EXPECT_EQ(request.data().size(), expect.size());
      EXPECT_TRUE(
          std::equal(request.data().begin(), request.data().end(), expect.begin(), expect.end()));

      completer.Reply(fit::success());
    }

    // Neither Receive nor ExpectReceive should block the thread. If either blocks the thread,
    // execution will not be able to proceed.
    void Receive(ReceiveCompleter::Sync& completer) override {
      fbl::AutoLock _(&lock_);
      if (expect_receive_.empty()) {
        receive_completer_.emplace(completer.ToAsync());
        return;
      }
      completer.Reply(fit::ok(std::move(expect_receive_.front())));
      expect_receive_.pop();
    }
    void ExpectReceive(std::vector<uint8_t> vec) {
      fbl::AutoLock _(&lock_);
      if (receive_completer_.has_value()) {
        receive_completer_->Reply(fit::ok(std::move(vec)));
        receive_completer_.reset();
        return;
      }
      expect_receive_.push(std::move(vec));
    }
    void ExpectReceive(apacket packet) {
      std::vector<uint8_t> vec;
      std::copy(reinterpret_cast<uint8_t*>(&packet.msg),
                reinterpret_cast<uint8_t*>(&packet.msg) + sizeof(amessage),
                std::back_inserter(vec));
      std::copy(packet.payload.data(), packet.payload.data() + packet.payload.size(),
                std::back_inserter(vec));
      ExpectReceive(std::move(vec));
    }

    //  private:
    friend class FakeAdbDriver;

    async::Loop loop_;
    std::optional<fidl::ServerBindingRef<fuchsia_hardware_adb::UsbAdbImpl>> binding_;

    fbl::Mutex lock_;
    std::queue<std::vector<uint8_t>> expect_queue_tx_ __TA_GUARDED(lock_);
    std::queue<std::vector<uint8_t>> expect_receive_ __TA_GUARDED(lock_);
    std::optional<ReceiveCompleter::Async> receive_completer_ __TA_GUARDED(lock_);
  } impl_;

  class DeviceServer : public fidl::Server<fuchsia_hardware_adb::Device> {
   public:
    explicit DeviceServer(FakeAdbDriver* adb) : impl_(&adb->impl_) {}

    void Start(StartRequest& request, StartCompleter::Sync& completer) override {
      ASSERT_TRUE(expect_start_ > 0);
      expect_start_--;
      impl_->Bind(std::move(request.interface()));

      completer.Reply(fit::success());
    }

   private:
    friend class FakeAdbDriver;
    UsbAdbImplServer* impl_;

    std::atomic_uint32_t expect_start_ = 0;
  } device_;
};

class FakeAdb : public Adb, public component_testing::LocalComponentImpl {
 public:
  explicit FakeAdb(async_dispatcher_t* dispatcher) : Adb(dispatcher) {}

  // component_testing::LocalComponentImpl methods
  void OnStart() override { started_ = true; }

  bool IsStarted() const { return started_; }

  void OnStop() override { started_ = false; }

  zx::result<zx::socket> GetServiceSocket(std::string_view service_name,
                                          std::string_view args) override {
    if (!client_.is_valid()) {
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok(std::move(client_));
  }

  void SetServiceSocket(zx::socket client) { client_ = std::move(client); }

 private:
  friend class AdbTest;

  bool started_ = false;
  fidl::BindingSet<fuchsia::hardware::adb::Provider> bindings_;
  zx::socket client_;
};

class AdbTest : public testing::Test {
 public:
  AdbTest()
      : fidl_loop_(&kAsyncLoopConfigNeverAttachToThread),
        dev_(std::make_unique<FakeAdb>(fidl_loop_.dispatcher())) {
    EXPECT_EQ(fidl_loop_.StartThread("adb-test-fidl-thread"), ZX_OK);
  }

  void SetUp() override {
    // The test device connector
    class TestConnector : public DeviceConnector {
     public:
      explicit TestConnector(async_dispatcher_t* dispatcher, FakeAdbDriver* fake_driver)
          : dispatcher_(dispatcher), fake_driver_(fake_driver) {}

      zx::result<fidl::ClientEnd<fuchsia_hardware_adb::Device>> ConnectToFirstDevice() override {
        zx::channel server, client;
        EXPECT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
        driver_binding_.emplace(fidl::BindServer(
            dispatcher_, fidl::ServerEnd<fuchsia_hardware_adb::Device>(std::move(server)),
            &(fake_driver_->device_)));
        return zx::ok(fidl::ClientEnd<fuchsia_hardware_adb::Device>(std::move(client)));
      }

     private:
      async_dispatcher_t* dispatcher_;
      std::optional<fidl::ServerBindingRef<fuchsia_hardware_adb::Device>> driver_binding_;
      FakeAdbDriver* fake_driver_;
    } test_connector(fidl_loop_.dispatcher(), &fake_driver_);
    fake_driver_.ExpectStart();
    EXPECT_EQ(dev_->Init(&test_connector), ZX_OK);
  }
  void TearDown() override {
    while (!fake_driver_.expectations_empty()) {
      usleep(1'000);
    }
    dev_.reset();
    fake_driver_.TearDown();
    fidl_loop_.Shutdown();
  }

 private:
 protected:
  async::Loop fidl_loop_;
  FakeAdbDriver fake_driver_;
  std::unique_ptr<FakeAdb> dev_;
};

TEST_F(AdbTest, SendUsbPacketTest) {
  uint8_t test[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
  fake_driver_.ExpectQueueTx(0, std::vector(test, test + sizeof(test)));
  dev_->SendUsbPacket(test, sizeof(test));
}

TEST_F(AdbTest, ConnectTest) {
  // Send A_CNXN
  std::string connection_string =
      "device::ro.product.name=zircon;ro.product.model=zircon;ro.product.device=zircon;";
  fake_driver_.ExpectQueueTx(
      0, {
             .msg =
                 {
                     .command = A_CNXN,
                     .arg0 = A_VERSION,
                     .arg1 = MAX_PAYLOAD,
                     .data_length = static_cast<uint32_t>(connection_string.size()),
                     .data_check = 0,
                     .magic = A_CNXN ^ 0xffffffff,
                 },
             .payload = {},
         });
  fake_driver_.ExpectQueueTx(
      0, std::vector<uint8_t>(connection_string.begin(), connection_string.end()));
  fake_driver_.SendConnect();
}

// Integration tests
class FakeAdbServiceProvider : public fuchsia::hardware::adb::Provider,
                               public component_testing::LocalComponentImpl {
 public:
  explicit FakeAdbServiceProvider(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // fuchsia_hardware_adb::Provider methods
  void ConnectToService(zx::socket socket, fidl::StringPtr args,
                        ConnectToServiceCallback callback) override {
    socket_ = std::move(socket);
    callback(fuchsia::hardware::adb::Provider_ConnectToService_Result::WithResponse(
        fuchsia::hardware::adb::Provider_ConnectToService_Response()));
  }

  // component_testing::LocalComponentImpl methods
  void OnStart() override {
    started_ = true;
    ASSERT_EQ(outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)), ZX_OK);
  }

  bool IsStarted() const { return started_; }

  void OnStop() override { started_ = false; }

  zx::socket& socket() { return socket_; }

 private:
  bool started_ = false;
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::hardware::adb::Provider> bindings_;
  zx::socket socket_;
};

class AdbRealmTest : public AdbTest, public loop_fixture::RealLoop {
 public:
  void SetUp() override {
    AdbTest::SetUp();
    SendConnect();
    BuildRealm();
  }

  void TearDown() override {
    realm_.reset();
    AdbTest::TearDown();
  }

  void SendConnect() {
    // Send A_CNXN
    std::string connection_string =
        "device::ro.product.name=zircon;ro.product.model=zircon;ro.product.device=zircon;";
    fake_driver_.ExpectQueueTx(
        0, {
               .msg =
                   {
                       .command = A_CNXN,
                       .arg0 = A_VERSION,
                       .arg1 = MAX_PAYLOAD,
                       .data_length = static_cast<uint32_t>(connection_string.size()),
                       .data_check = 0,
                       .magic = A_CNXN ^ 0xffffffff,
                   },
               .payload = {},
           });
    fake_driver_.ExpectQueueTx(
        0, std::vector<uint8_t>(connection_string.begin(), connection_string.end()));
    fake_driver_.SendConnect();
    while (!fake_driver_.expectations_empty()) {
      usleep(1'000);
    }
  }

  void BuildRealm() {
    auto builder = RealmBuilder::Create();
    dev_ptr_ = dev_.get();
    builder.AddLocalChild(
        "adb", [&]() { return std::move(dev_); }, ChildOptions{.startup_mode = StartupMode::EAGER});

    auto service_provider = std::make_unique<FakeAdbServiceProvider>(dispatcher());
    service_provider_ptr_ = service_provider.get();

    // Add component to the realm, providing a mock implementation
    builder.AddLocalChild(
        std::string(kShellService),
        [provider = std::move(service_provider)]() mutable { return std::move(provider); },
        ChildOptions{});

    builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::hardware::adb::Provider::Name_}},
                           .source = ChildRef{std::string(kShellService)},
                           .targets = {ParentRef()}});
    realm_ = std::make_unique<RealmRoot>(builder.Build(dispatcher()));
    RunLoopUntil([&]() { return dev_ptr_->IsStarted(); });
  }

 protected:
  std::unique_ptr<RealmRoot> realm_;
  FakeAdbServiceProvider* service_provider_ptr_ = nullptr;
  FakeAdb* dev_ptr_ = nullptr;
};

TEST_F(AdbRealmTest, ServiceConnectTest) {
  auto provider = realm_->Connect<fuchsia::hardware::adb::Provider>();

  zx::socket server, client;
  EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &server, &client), ZX_OK);
  provider->ConnectToService(std::move(server), "",
                             [&](fuchsia::hardware::adb::Provider_ConnectToService_Result result) {
                               EXPECT_FALSE(result.is_err());
                               QuitLoop();
                             });

  RunLoop();
}

TEST_F(AdbRealmTest, ServiceOpenCloseTest) {
  auto provider = realm_->Connect<fuchsia::hardware::adb::Provider>();
  zx::socket server, client;
  EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &server, &client), ZX_OK);

  provider->ConnectToService(std::move(server), "",
                             [&](fuchsia::hardware::adb::Provider_ConnectToService_Result result) {
                               EXPECT_FALSE(result.is_err());
                               QuitLoop();
                             });

  RunLoop();
  dev_ptr_->SetServiceSocket(std::move(client));

  // Send A_OPEN
  fake_driver_.ExpectQueueTx(0, {
                                    .msg =
                                        {
                                            .command = A_OKAY,
                                            .arg0 = 1,  // local-id
                                            .arg1 = 1,  // remote-id
                                            .data_length = 0,
                                            .data_check = 0,
                                            .magic = A_OKAY ^ 0xffffffff,
                                        },
                                    .payload = {},
                                });
  fake_driver_.SendOpen({'s', 'h', 'e', 'l', 'l', ':', '\0'});

  while (!fake_driver_.expectations_empty()) {
    usleep(1'000);
  }

  // Expect A_CLSE
  fake_driver_.ExpectQueueTx(0, {
                                    .msg =
                                        {
                                            .command = A_CLSE,
                                            .arg0 = 1,  // local-id
                                            .arg1 = 1,  // remote-id
                                            .data_length = 0,
                                            .data_check = 0,
                                            .magic = A_CLSE ^ 0xffffffff,
                                        },
                                    .payload = {},
                                });
  service_provider_ptr_->socket().reset();
  while (!fake_driver_.expectations_empty()) {
    usleep(1'000);
  }
}

TEST_F(AdbRealmTest, ServiceReadWriteTest) {
  auto provider = realm_->Connect<fuchsia::hardware::adb::Provider>();
  zx::socket server, client;
  EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &server, &client), ZX_OK);

  provider->ConnectToService(std::move(server), "",
                             [&](fuchsia::hardware::adb::Provider_ConnectToService_Result result) {
                               EXPECT_FALSE(result.is_err());
                               QuitLoop();
                             });

  RunLoop();
  dev_ptr_->SetServiceSocket(std::move(client));

  uint8_t helloworld[] = {'H', 'e', 'l', 'l', 'o', 'W', 'o', 'r', 'l', 'd', '\0'};
  size_t actual;

  // Open service
  fake_driver_.ExpectQueueTx(0, {
                                    .msg =
                                        {
                                            .command = A_OKAY,
                                            .arg0 = 1,  // local-id
                                            .arg1 = 1,  // remote-id
                                            .data_length = 0,
                                            .data_check = 0,
                                            .magic = A_OKAY ^ 0xffffffff,
                                        },
                                    .payload = {},
                                });
  fake_driver_.SendOpen({'s', 'h', 'e', 'l', 'l', ':', '\0'});

  // Test sending data to the service.
  fake_driver_.ExpectQueueTx(0, {
                                    .msg =
                                        {
                                            .command = A_OKAY,
                                            .arg0 = 1,  // local-id
                                            .arg1 = 1,  // remote-id
                                            .data_length = 0,
                                            .data_check = 0,
                                            .magic = A_OKAY ^ 0xffffffff,
                                        },
                                    .payload = {},
                                });
  fake_driver_.SendWrite(std::vector<uint8_t>(helloworld, helloworld + sizeof(helloworld)));
  while (!fake_driver_.expectations_empty()) {
    usleep(1'000);
  }

  zx_signals_t signal;
  ASSERT_EQ(service_provider_ptr_->socket().wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                                                     zx::deadline_after(zx::duration::infinite()),
                                                     &signal),
            ZX_OK);
  ASSERT_FALSE((signal & ZX_SOCKET_READABLE) == 0);
  char out_helloworld[11];
  EXPECT_EQ(
      service_provider_ptr_->socket().read(0, out_helloworld, sizeof(out_helloworld), &actual),
      ZX_OK);
  EXPECT_EQ(actual, sizeof(out_helloworld));

  EXPECT_EQ(sizeof(helloworld), sizeof(out_helloworld));
  for (size_t i = 0; i < sizeof(helloworld); i++) {
    EXPECT_EQ(helloworld[i], out_helloworld[i]);
  }

  // Test service sending data to the client.
  fake_driver_.ExpectQueueTx(0, {
                                    .msg =
                                        {
                                            .command = A_WRTE,
                                            .arg0 = 1,  // local-id
                                            .arg1 = 1,  // remote-id
                                            .data_length = sizeof(helloworld),
                                            .data_check = 0,
                                            .magic = A_WRTE ^ 0xffffffff,
                                        },
                                    .payload = {},
                                });
  // Payload is sent in the next packet.
  std::vector<uint8_t> expect_data(sizeof(helloworld));
  std::copy(helloworld, helloworld + sizeof(helloworld), expect_data.begin());
  fake_driver_.ExpectQueueTx(0, expect_data);
  ASSERT_EQ(service_provider_ptr_->socket().write(0, helloworld, sizeof(helloworld), &actual),
            ZX_OK);
  ASSERT_EQ(actual, sizeof(helloworld));

  while (!fake_driver_.expectations_empty()) {
    usleep(1'000);
  }

  // Expect A_CLSE
  fake_driver_.ExpectQueueTx(0, {
                                    .msg =
                                        {
                                            .command = A_CLSE,
                                            .arg0 = 1,  // local-id
                                            .arg1 = 1,  // remote-id
                                            .data_length = 0,
                                            .data_check = 0,
                                            .magic = A_CLSE ^ 0xffffffff,
                                        },
                                    .payload = {},
                                });
  service_provider_ptr_->socket().reset();
  while (!fake_driver_.expectations_empty()) {
    usleep(1'000);
  }
}

}  // namespace adb
