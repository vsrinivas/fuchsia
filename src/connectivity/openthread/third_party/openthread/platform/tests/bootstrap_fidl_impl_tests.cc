#include <fuchsia/thread/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>
#include <src/lib/fsl/vmo/strings.h>

#include "bootstrap_fidl_impl.h"

// Contents of thread settings file on Fuchsia
// Keys are decimal representation of uint16_t in double quotes.
// Values are array of base64 encoded strings
static constexpr char kContents[] =
    "{\"1\":[\"DggAAAAAAAAAAAADAAALNQYABAAf/+ACCN6tAL7vAMr+Bwj93q0Avu8AAAUQABEiM"
    "0RVZneImaq7zN3u/wMEdGVzdAECiw4EEDyaLFHv43sLyfgCDs5AuVYMAwKg/w==\"],"
    "\"3\":[\"BA8ADAAAAADrAwAA6AMAACPJ7SOmDXiVYRDVGgGQ+NpGZsaJAgA=\"]}";

class TestableBootstrapImpl : public ot::Fuchsia::BootstrapImpl {
 public:
  TestableBootstrapImpl(sys::ComponentContext* context) : BootstrapImpl(context) {}
  void SetShouldServe(bool value) { should_serve_ = value; }

  void SetSettingsPath(std::string config_path) { config_path_ = std::move(config_path); }

 private:
  std::string GetSettingsPath() override { return config_path_; }
  bool ShouldServe() override { return should_serve_; }

  std::string config_path_;
  bool should_serve_;
};

class BootstrapImplTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    // Set up BootstrapImpl.
    ResetImpl(/*should_serve*/ true);

    // Connect to the interface under test.
    bootstrap_.set_error_handler([this](zx_status_t error) { last_error_ = error; });
    ReconnectBootstrapPtr();
    ASSERT_TRUE(bootstrap_.is_bound());
  }

 protected:
  fuchsia::thread::BootstrapPtr& bootstrap() { return bootstrap_; }
  TestableBootstrapImpl& bootstrap_impl() { return *bootstrap_impl_; }

  void ResetImpl(bool should_serve) {
    bootstrap_impl_ = std::make_unique<TestableBootstrapImpl>(provider_.context());
    bootstrap_impl_->SetShouldServe(should_serve);
    bootstrap_impl_->Init();
    RunLoopUntilIdle();
  }

  void ReconnectBootstrapPtr() {
    last_error_ = ZX_OK;
    provider_.ConnectToPublicService(bootstrap_.NewRequest());
    RunLoopUntilIdle();
  }

  zx_status_t last_error() { return last_error_; }

 private:
  sys::testing::ComponentContextProvider provider_;
  fuchsia::thread::BootstrapPtr bootstrap_;
  std::unique_ptr<TestableBootstrapImpl> bootstrap_impl_;
  zx_status_t last_error_;
};

// Test Cases ------------------------------------------------------------------

TEST_F(BootstrapImplTest, NoServe) {
  ResetImpl(/*should_serve*/ false);
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, last_error());
  ReconnectBootstrapPtr();
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, last_error());
}

TEST_F(BootstrapImplTest, ImportSettingsHappy) {
  constexpr char kSettingsPath[] = "/data/config-happy.json";
  bootstrap_impl().SetSettingsPath(kSettingsPath);

  fuchsia::mem::Buffer buffer;

  ASSERT_TRUE(fsl::VmoFromString(kContents, &buffer));

  EXPECT_TRUE(bootstrap().is_bound());
  bool called = false;
  bootstrap()->ImportThreadSettings(std::move(buffer), [&called]() { called = true; });
  RunLoopUntilIdle();

  // Confirm that callback function was called
  EXPECT_TRUE(called);

  std::string contents;
  files::ReadFileToString(kSettingsPath, &contents);

  EXPECT_EQ(kContents, contents);

  // Ensure binding is closed and FIDL is no longer serving.
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_OK, last_error());

  ReconnectBootstrapPtr();
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, last_error());
}

TEST_F(BootstrapImplTest, ImportSettingsFailUnreadable) {
  constexpr char kSettingsPath[] = "/data/config-fail.json";
  bootstrap_impl().SetSettingsPath(kSettingsPath);

  fuchsia::mem::Buffer buffer;

  ASSERT_TRUE(fsl::VmoFromString(kContents, &buffer));

  // Restrict rights to cause read failure.
  zx::vmo temp;
  buffer.vmo.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER, &temp);
  buffer.vmo = std::move(temp);

  EXPECT_TRUE(bootstrap().is_bound());
  bool called = false;
  bootstrap()->ImportThreadSettings(std::move(buffer), [&called]() { called = true; });
  RunLoopUntilIdle();

  // Confirm that callback wasn't called:
  EXPECT_FALSE(called);
  EXPECT_EQ(last_error(), ZX_ERR_IO);

  // Ensure binding closed. So if binding is closed but callback isn't called
  // this indicates that there was some error
  EXPECT_FALSE(bootstrap().is_bound());

  ReconnectBootstrapPtr();
  EXPECT_FALSE(bootstrap().is_bound());
}


TEST_F(BootstrapImplTest, ImportSettingsFailNonWritable) {
  // Use  non-existent-dir to trigger write permission failure
  constexpr char kSettingsPath[] = "/non-existent-dir/config-fail.json";
  bootstrap_impl().SetSettingsPath(kSettingsPath);

  fuchsia::mem::Buffer buffer;

  ASSERT_TRUE(fsl::VmoFromString(kContents, &buffer));

  EXPECT_TRUE(bootstrap().is_bound());
  bool called = false;
  bootstrap()->ImportThreadSettings(std::move(buffer), [&called]() { called = true; });
  RunLoopUntilIdle();

  // Confirm that callback wasn't called:
  EXPECT_FALSE(called);
  EXPECT_EQ(last_error(), ZX_ERR_IO);

  // Ensure binding closed. So if binding is closed but callback isn't called
  // this indicates that there was some error
  EXPECT_FALSE(bootstrap().is_bound());

  ReconnectBootstrapPtr();
  EXPECT_FALSE(bootstrap().is_bound());
}
