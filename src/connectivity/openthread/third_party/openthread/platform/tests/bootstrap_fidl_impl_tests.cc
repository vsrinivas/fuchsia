#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/svc/dir.h>
#include <lib/svc/outgoing.h>

#include <unordered_map>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>
#include <src/lib/fsl/vmo/strings.h>

#include "bootstrap_fidl_impl.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

// Contents of thread settings file on Fuchsia
// Keys are decimal representation of uint16_t in double quotes.
// Values are array of base64 encoded strings
static constexpr char kContents[] =
    "{\"1\":[\"DggAAAAAAAAAAAADAAALNQYABAAf/+ACCN6tAL7vAMr+Bwj93q0Avu8AAAUQABEiM"
    "0RVZneImaq7zN3u/wMEdGVzdAECiw4EEDyaLFHv43sLyfgCDs5AuVYMAwKg/w==\"],"
    "\"3\":[\"BA8ADAAAAADrAwAA6AMAACPJ7SOmDXiVYRDVGgGQ+NpGZsaJAgA=\"]}";

class TestableBootstrapThreadImpl : public ot::Fuchsia::BootstrapThreadImpl {
 public:
  TestableBootstrapThreadImpl() : BootstrapThreadImpl() {}
  void SetShouldServe(bool value) { should_serve_ = value; }

  void SetSettingsPath(std::string config_path) { config_path_ = std::move(config_path); }

 private:
  std::string GetSettingsPath() override { return config_path_; }
  bool ShouldServe() override { return should_serve_; }

  std::string config_path_;
  bool should_serve_;
};

class BootstrapThreadImplTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    auto endpoints = fidl::CreateEndpoints<fuchsia_lowpan_bootstrap::Thread>();
    ASSERT_FALSE(endpoints.is_error());

    ResetServerImpl(/* ShouldServe */ true, std::move(endpoints->server));
    ReconnectClient(std::move(endpoints->client));
  }

 protected:
  fidl::WireClient<fuchsia_lowpan_bootstrap::Thread>& bootstrap_client() {
    return bootstrap_client_;
  }
  TestableBootstrapThreadImpl& bootstrap_impl() { return *bootstrap_impl_; }

  void ResetServerImpl(bool should_serve,
                       fidl::ServerEnd<fuchsia_lowpan_bootstrap::Thread> request) {
    bootstrap_impl_ = std::make_unique<TestableBootstrapThreadImpl>();
    bootstrap_impl_->SetShouldServe(should_serve);
    auto bind_status = bootstrap_impl_->Bind(std::move(request), dispatcher(), std::nullopt);
    EXPECT_EQ(bind_status, ZX_OK);
    RunLoopUntilIdle();
  }

  void ReconnectClient(fidl::ClientEnd<fuchsia_lowpan_bootstrap::Thread> client_end) {
    event_handler_ = EventHandler{};
    event_handler_.client_bound_ = true;
    event_handler_.client_unbind_status_ = ZX_OK;
    bootstrap_client_ = fidl::WireClient(std::move(client_end), dispatcher(), &event_handler_);
    RunLoopUntilIdle();
  }

  bool client_bound() { return event_handler_.client_bound_; }
  zx_status_t client_unbind_status() { return event_handler_.client_unbind_status_; }

 private:
  fidl::WireClient<fuchsia_lowpan_bootstrap::Thread> bootstrap_client_;
  std::unique_ptr<TestableBootstrapThreadImpl> bootstrap_impl_;

  // Event handler on client side:
  class EventHandler : public fidl::WireAsyncEventHandler<fuchsia_lowpan_bootstrap::Thread> {
   public:
    EventHandler() = default;

    void on_fidl_error(fidl::UnbindInfo info) override {
      client_bound_ = false;
      client_unbind_status_ = info.status();
    }

    bool client_bound_ = false;
    zx_status_t client_unbind_status_ = ZX_OK;
  };

  EventHandler event_handler_;
};

// Test Cases ------------------------------------------------------------------

TEST_F(BootstrapThreadImplTest, NoServe) {
  EXPECT_TRUE(client_bound());

  auto endpoints = fidl::CreateEndpoints<fuchsia_lowpan_bootstrap::Thread>();
  EXPECT_FALSE(endpoints.is_error());

  ResetServerImpl(/*should_serve*/ false, std::move(endpoints->server));

  EXPECT_FALSE(client_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, client_unbind_status());

  ReconnectClient(std::move(endpoints->client));

  EXPECT_FALSE(client_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, client_unbind_status());
}

TEST_F(BootstrapThreadImplTest, ImportSettingsHappy) {
  constexpr char kSettingsPath[] = "/data/config-happy.json";
  bootstrap_impl().SetSettingsPath(kSettingsPath);

  // Get the variables prepared for calling the fidl function
  fuchsia_mem::wire::Buffer buffer;
  {
    fuchsia::mem::Buffer temp_buffer;
    ASSERT_TRUE(fsl::VmoFromString(kContents, &temp_buffer));
    buffer.vmo = std::move(temp_buffer.vmo);
    buffer.size = temp_buffer.size;
  }

  // Make the call from client side and ensure callback gets called:
  EXPECT_TRUE(client_bound());
  bool called = false;
  bootstrap_client()
      ->ImportSettings(std::move(buffer))
      .ThenExactlyOnce(
          [&called](
              fidl::WireUnownedResult<fuchsia_lowpan_bootstrap::Thread::ImportSettings>& result) {
            EXPECT_TRUE(result.ok());
            EXPECT_EQ(result.status(), ZX_OK);
            called = true;
          });

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  // Confirm the contents of the file are as expected:
  std::string contents;
  files::ReadFileToString(kSettingsPath, &contents);
  EXPECT_EQ(kContents, contents);

  EXPECT_FALSE(client_bound());
  EXPECT_EQ(client_unbind_status(), ZX_OK);
}

TEST_F(BootstrapThreadImplTest, ImportSettingsFailUnreadable) {
  constexpr char kSettingsPath[] = "/data/config-fail.json";
  bootstrap_impl().SetSettingsPath(kSettingsPath);

  // Get the variables prepared for calling the fidl function
  fuchsia_mem::wire::Buffer buffer;
  {
    fuchsia::mem::Buffer temp_buffer;
    ASSERT_TRUE(fsl::VmoFromString(kContents, &temp_buffer));

    // Restrict rights to cause read failure:
    zx::vmo vmo_restricted;
    temp_buffer.vmo.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER, &vmo_restricted);

    buffer.vmo = std::move(vmo_restricted);
    buffer.size = temp_buffer.size;
  }

  // Make the call from client side:
  EXPECT_TRUE(client_bound());
  bool errored = false;
  bootstrap_client()
      ->ImportSettings(std::move(buffer))
      .ThenExactlyOnce(
          [&errored](
              fidl::WireUnownedResult<fuchsia_lowpan_bootstrap::Thread::ImportSettings>& result) {
            // Confirm that the call failed:
            ASSERT_EQ(result.status(), ZX_ERR_IO);
            ASSERT_EQ(result.reason(), fidl::Reason::kPeerClosed);
            errored = true;
          });

  RunLoopUntilIdle();
  EXPECT_TRUE(errored);

  // Confirm that channel was closed with error:
  EXPECT_FALSE(client_bound());
  EXPECT_EQ(client_unbind_status(), ZX_ERR_IO);
}

TEST_F(BootstrapThreadImplTest, ImportSettingsFailNonWritable) {
  // Use  non-existent-dir to trigger write permission failure
  constexpr char kSettingsPath[] = "/non-existent-dir/config-fail.json";
  bootstrap_impl().SetSettingsPath(kSettingsPath);

  // Get the variables prepared for calling the fidl function
  fuchsia_mem::wire::Buffer buffer;
  {
    fuchsia::mem::Buffer temp_buffer;
    ASSERT_TRUE(fsl::VmoFromString(kContents, &temp_buffer));
    buffer.vmo = std::move(temp_buffer.vmo);
    buffer.size = temp_buffer.size;
  }

  // Make the call from client side:
  EXPECT_TRUE(client_bound());
  bool errored = false;
  bootstrap_client()
      ->ImportSettings(std::move(buffer))
      .ThenExactlyOnce(
          [&errored](
              fidl::WireUnownedResult<fuchsia_lowpan_bootstrap::Thread::ImportSettings>& result) {
            // Confirm that the call failed:
            ASSERT_EQ(result.status(), ZX_ERR_IO);
            ASSERT_EQ(result.reason(), fidl::Reason::kPeerClosed);
            errored = true;
          });

  RunLoopUntilIdle();
  EXPECT_TRUE(errored);

  // Confirm that channel was closed with error:
  EXPECT_FALSE(client_bound());
  EXPECT_EQ(client_unbind_status(), ZX_ERR_IO);
}
