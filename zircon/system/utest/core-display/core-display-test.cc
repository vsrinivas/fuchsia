// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>

// clang-format off
// Required because banjo defines some macros that conflict with LLCPP.
#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>
// clang-format on
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/pixelformat.h>
#include <zircon/rights.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace fhd = fuchsia_hardware_display;
namespace sysinfo = fuchsia_sysinfo;
namespace sysmem = fuchsia_sysmem;

namespace {

constexpr uint64_t kEventId = 13;
constexpr uint32_t kCollectionId = 12;
constexpr uint64_t kInvalidId = 34;

class CoreDisplayTest : public zxtest::Test {
 public:
  void SetUp() override;
  void TearDown() override;
  bool IsCaptureSupported() { return (dc_client_->IsCaptureSupported()->value()->supported); }
  void ImportEvent();
  void CreateToken();
  void DuplicateAndImportToken();
  void ImportBufferCollection();
  void SetBufferConstraints();
  void FinalizeClientConstraints();
  uint64_t ImportCaptureImage() const;
  zx_status_t StartCapture(uint64_t id, uint64_t e = kEventId) const;
  zx_status_t WaitForEvent();
  zx_status_t ReleaseCapture(uint64_t id) const;
  void CaptureSetup();

  fdio_cpp::FdioCaller caller_;

  fidl::WireSyncClient<fhd::Controller> dc_client_;
  fidl::WireSyncClient<sysinfo::SysInfo> sysinfo_;
  fidl::WireSyncClient<sysmem::Allocator> sysmem_allocator_;
  zx::event client_event_;
  fidl::WireSyncClient<sysmem::BufferCollectionToken> token_;
  fidl::WireSyncClient<sysmem::BufferCollection> collection_;

 private:
  fidl::ClientEnd<fhd::Controller> device_client_channel_;
  fbl::Vector<fhd::wire::Info> displays_;
  bool capture_supported_ = false;
};

void CoreDisplayTest::SetUp() {
  zx::result device_server_channel =
      fidl::CreateEndpoints<fhd::Controller>(&device_client_channel_);
  ASSERT_TRUE(device_server_channel.is_ok(), "%s", device_server_channel.status_string());

  zx::result dc_endpoints = fidl::CreateEndpoints<fhd::Controller>();
  ASSERT_TRUE(dc_endpoints.is_ok(), "%s", dc_endpoints.status_string());

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(open("/dev/class/display-controller/000", O_RDWR)), "%s",
              strerror(errno));
  caller_.reset(std::move(fd));

  const fidl::WireResult result = fidl::WireCall(caller_.borrow_as<fhd::Provider>())
                                      ->OpenController(device_server_channel.value().TakeChannel(),
                                                       std::move(dc_endpoints->server));
  ASSERT_TRUE(result.ok(), "%s", result.status_string());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.s);

  dc_client_ = fidl::WireSyncClient(std::move(dc_endpoints->client));

  class EventHandler : public fidl::WireSyncEventHandler<fhd::Controller> {
   public:
    EventHandler() = default;

    bool has_display() const { return has_display_; }
    const fbl::Vector<fhd::wire::Info>& displays_tmp() const { return displays_tmp_; }

    void OnDisplaysChanged(fidl::WireEvent<fhd::Controller::OnDisplaysChanged>* event) override {
      for (unsigned i = 0; i < event->added.count(); i++) {
        displays_tmp_.push_back(std::move(event->added[i]));
      }
      has_display_ = true;
    }

    void OnVsync(fidl::WireEvent<fhd::Controller::OnVsync>* event) override {}

    void OnClientOwnershipChange(
        fidl::WireEvent<fhd::Controller::OnClientOwnershipChange>* event) override {}

   private:
    bool has_display_ = false;
    fbl::Vector<fhd::wire::Info> displays_tmp_;
  };

  EventHandler event_handler;
  do {
    fidl::Status result = dc_client_.HandleOneEvent(event_handler);
    ASSERT_TRUE(result.ok());
  } while (!event_handler.has_display());

  // get sysmem
  zx::result sysmem_allocator = component::Connect<sysmem::Allocator>();
  ASSERT_TRUE(sysmem_allocator.is_ok(), "%s", sysmem_allocator.status_string());
  sysmem_allocator_ = fidl::WireSyncClient(std::move(sysmem_allocator.value()));
}

void CoreDisplayTest::TearDown() {
  if (collection_) {
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)collection_->Close();
  }
  sysmem_allocator_ = {};
}

void CoreDisplayTest::ImportEvent() {
  // First, import signal event to get notified when capture buffer has valid data
  zx::event e2;
  auto status = zx::event::create(0, &client_event_);
  ASSERT_OK(status);
  status = client_event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &e2);
  ASSERT_OK(status);
  auto event_status = dc_client_->ImportEvent(std::move(e2), kEventId);
  ASSERT_TRUE(event_status.ok());
}

void CoreDisplayTest::CreateToken() {
  // Create token and keep the client
  zx::result endpoints = fidl::CreateEndpoints<sysmem::BufferCollectionToken>();
  ASSERT_TRUE(endpoints.is_ok(), "%s", endpoints.status_string());

  // Pass token server to sysmem allocator
  const fidl::WireResult result =
      sysmem_allocator_->AllocateSharedCollection(std::move(endpoints->server));
  ASSERT_TRUE(result.ok(), "%s", result.status_string());

  token_ = fidl::WireSyncClient(std::move(endpoints->client));
}

void CoreDisplayTest::DuplicateAndImportToken() {
  // Duplicate the token, to be passed to the display controller
  zx::result endpoints = fidl::CreateEndpoints<sysmem::BufferCollectionToken>();
  ASSERT_TRUE(endpoints.is_ok(), "%s", endpoints.status_string());

  {
    const fidl::WireResult result =
        token_->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(endpoints->server));
    ASSERT_TRUE(result.ok(), "%s", result.status_string());
  }
  {
    const fidl::WireResult result = token_->Sync();
    ASSERT_TRUE(result.ok(), "%s", result.status_string());
  }

  const fidl::WireResult result =
      dc_client_->ImportBufferCollection(kCollectionId, std::move(endpoints->client));
  ASSERT_TRUE(result.ok(), "%s", result.status_string());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.res);
}

void CoreDisplayTest::SetBufferConstraints() {
  fhd::wire::ImageConfig image_config = {};
  image_config.type = IMAGE_TYPE_CAPTURE;
  auto constraints_resp = dc_client_->SetBufferCollectionConstraints(kCollectionId, image_config);
  ASSERT_TRUE(constraints_resp.ok());
  ASSERT_OK(constraints_resp.value().res);
}

void CoreDisplayTest::FinalizeClientConstraints() {
  // now that we have provided all that's needed to the display controllers, we can
  // return our token, set our own constraints and for allocation
  // Before that, we need to create a channel to communicate with the buffer collection
  zx::result endpoints = fidl::CreateEndpoints<sysmem::BufferCollection>();
  ASSERT_TRUE(endpoints.is_ok(), "%s", endpoints.status_string());

  {
    const fidl::WireResult result = sysmem_allocator_->BindSharedCollection(
        token_.TakeClientEnd(), std::move(endpoints->server));
    ASSERT_TRUE(result.ok(), "%s", result.status_string());
  }

  // token has been returned. Let's set contraints
  sysmem::wire::BufferCollectionConstraints constraints = {};
  constraints.usage.cpu = sysmem::wire::kCpuUsageReadOften | sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = false;
  constraints.image_format_constraints_count = 1;
  sysmem::wire::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgra32;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = sysmem::wire::ColorSpace{
      .type = sysmem::wire::ColorSpaceType::kSrgb,
  };
  image_constraints.min_coded_width = 0;
  image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints.min_coded_height = 0;
  image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 1;
  image_constraints.coded_height_divisor = 1;
  image_constraints.bytes_per_row_divisor = 1;
  image_constraints.start_offset_divisor = 1;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  collection_ = fidl::WireSyncClient(std::move(endpoints->client));
  {
    const fidl::WireResult result = collection_->SetConstraints(true, constraints);
    ASSERT_TRUE(result.ok(), "%s", result.status_string());
  }

  // Token return and constraints set. Wait for allocation
  {
    const fidl::WireResult result = collection_->WaitForBuffersAllocated();
    ASSERT_TRUE(result.ok(), "%s", result.status_string());
    const auto& response = result.value();
    ASSERT_OK(response.status);
  }
}

uint64_t CoreDisplayTest::ImportCaptureImage() const {
  // Make the buffer available for capture
  fhd::wire::ImageConfig capture_cfg = {};  // will contain a handle
  auto importcap_resp = dc_client_->ImportImageForCapture(capture_cfg, kCollectionId, 0);
  if (importcap_resp.status() != ZX_OK) {
    return INVALID_ID;
  }
  if (importcap_resp->is_error()) {
    return importcap_resp->error_value();
  }

  return importcap_resp->value()->image_id;
}

zx_status_t CoreDisplayTest::StartCapture(uint64_t id, uint64_t e) const {
  auto startcap_resp = dc_client_->StartCapture(e, id);
  if (!startcap_resp.ok()) {
    return startcap_resp.status();
  }
  if (startcap_resp->is_error()) {
    return (startcap_resp->error_value());
  }
  return ZX_OK;
}

zx_status_t CoreDisplayTest::ReleaseCapture(uint64_t id) const {
  auto releasecap_resp = dc_client_->ReleaseCapture(id);
  if (!releasecap_resp.ok()) {
    return releasecap_resp.status();
  }
  if (releasecap_resp->is_error()) {
    return (releasecap_resp->error_value());
  }
  return ZX_OK;
}

zx_status_t CoreDisplayTest::WaitForEvent() {
  uint32_t observed;
  auto event_res =
      client_event_.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::sec(1)), &observed);
  if (event_res == ZX_OK) {
    client_event_.signal(ZX_EVENT_SIGNALED, 0);
  }
  return event_res;
}

void CoreDisplayTest::CaptureSetup() {
  // First, import signal event to get notified when capture buffer has valid data
  ImportEvent();
  CreateToken();
  DuplicateAndImportToken();

  // Need to set constraints for allocation to occur
  SetBufferConstraints();

  // Pass back our own token and set our constraints so buffers can be allocated
  FinalizeClientConstraints();
}

TEST_F(CoreDisplayTest, CoreDisplayAlreadyBoundTest) {
  zx::result device_endpoints = fidl::CreateEndpoints<fhd::Controller>();
  ASSERT_TRUE(device_endpoints.is_ok(), "%s", device_endpoints.status_string());

  zx::result dc_endpoints = fidl::CreateEndpoints<fhd::Controller>();
  ASSERT_TRUE(dc_endpoints.is_ok(), "%s", dc_endpoints.status_string());

  const fidl::WireResult result =
      fidl::WireCall(caller_.borrow_as<fhd::Provider>())
          ->OpenController(device_endpoints->server.TakeChannel(), std::move(dc_endpoints->server));
  ASSERT_TRUE(result.ok(), "%s", result.status_string());
  const fidl::WireResponse response = result.value();
  ASSERT_STATUS(response.s, ZX_ERR_ALREADY_BOUND);
}

TEST_F(CoreDisplayTest, CreateLayer) {
  auto resp = dc_client_->CreateLayer();
  EXPECT_TRUE(resp.ok());
  EXPECT_EQ(ZX_OK, resp.value().res);
  EXPECT_EQ(1, resp.value().layer_id);
}

TEST_F(CoreDisplayTest, CaptureClientDeadAfterStart) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  // Make the buffer available for capture
  uint64_t id = ImportCaptureImage();
  ASSERT_NE(INVALID_ID, id);

  ASSERT_OK(StartCapture(id));

  // close client before capture completes
  dc_client_ = {};
}

TEST_F(CoreDisplayTest, CaptureFull) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  // Make the buffer available for capture
  uint64_t id = ImportCaptureImage();
  ASSERT_NE(INVALID_ID, id);

  ASSERT_OK(StartCapture(id));

  // wait for signal
  EXPECT_OK(WaitForEvent());

  // stop capture
  ASSERT_OK(ReleaseCapture(id));

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, MultipleCaptureFull) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  // Make the buffer available for capture
  uint64_t id = ImportCaptureImage();
  ASSERT_NE(INVALID_ID, id);

  for (int i = 0; i < 10; i++) {
    ASSERT_OK(StartCapture(id));

    // wait for signal
    EXPECT_OK(WaitForEvent());
  }

  // stop capture
  ASSERT_OK(ReleaseCapture(id));

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, CaptureReleaseAfterStart) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  // Make the buffer available for capture
  uint64_t id = ImportCaptureImage();
  ASSERT_NE(INVALID_ID, id);

  ASSERT_OK(StartCapture(id));
  EXPECT_OK(ReleaseCapture(id));

  // This will still get delivered
  EXPECT_OK(WaitForEvent());

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, InvalidStartCaptureId) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartCapture(kInvalidId));

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, InvalidStartEventId) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  // Make the buffer available for capture
  uint64_t id = ImportCaptureImage();
  ASSERT_NE(INVALID_ID, id);

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartCapture(id, kInvalidId));

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, MultipleCapture) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  uint64_t id = ImportCaptureImage();
  ASSERT_NE(INVALID_ID, id);

  ASSERT_OK(StartCapture(id));
  EXPECT_EQ(ZX_ERR_SHOULD_WAIT, StartCapture(id));

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, InvalidReleaseCaptureId) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ReleaseCapture(kInvalidId));

  // done. Close sysmem
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, CaptureNotSupported) {
  if (IsCaptureSupported()) {
    printf("Test Skipped\n");
    return;
  }
  fhd::wire::ImageConfig image_config = {};
  auto import_resp = dc_client_->ImportImageForCapture(image_config, 0, 0);
  EXPECT_TRUE(import_resp.ok());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, import_resp->error_value());

  auto start_resp = dc_client_->StartCapture(0, 0);
  EXPECT_TRUE(start_resp.ok());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, start_resp->error_value());

  auto release_resp = dc_client_->ReleaseCapture(0);
  EXPECT_TRUE(release_resp.ok());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, release_resp->error_value());
}

TEST_F(CoreDisplayTest, CreateLayerNoResource) {
  for (int i = 0; i < 65536; i++) {
    auto resp = dc_client_->CreateLayer();
    EXPECT_TRUE(resp.ok());
    EXPECT_EQ(ZX_OK, resp.value().res);
    EXPECT_EQ(i + 1, resp.value().layer_id);
  }

  auto resp = dc_client_->CreateLayer();
  EXPECT_TRUE(resp.ok());
  EXPECT_EQ(ZX_ERR_NO_RESOURCES, resp.value().res);
}

#if 0
TEST_F(CoreDisplayTest, DestroyLayer) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetDisplayMode) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetDisplayColorConversion) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetDisplayLayers) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerPrimaryConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerPrimaryPosition) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerPrimaryAlpha) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerCursorConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerCursorPosition) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerColorConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerImage) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, CheckConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ApplyConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, EnableVsync) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetVirtconMode) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ImportBufferCollection) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ReleaseBufferCollection) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetBufferCollectionConstraints) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ImportImage) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ReleaseImage) { EXPECT_OK(ZX_OK); }

// There is no return for this defined. Cannot verify if it actually
// worked or not
TEST_F(CoreDisplayTest, ImportEvent) { EXPECT_OK(ZX_OK); }

// There is no return for this defined. Cannot verify if it actually
// worked or not
TEST_F(CoreDisplayTest, ReleaseEvent) { EXPECT_OK(ZX_OK); }
#endif

}  // namespace

int main(int argc, char** argv) {
  constexpr char kDriverPath[] = "/dev/display/fake-display";
  if (access(kDriverPath, F_OK) != -1) {
    zxtest::RunAllTests(argc, argv);
  }
  return 0;
}
