// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/pixelformat.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>
#include <string_view>

#include <ddk/protocol/display/controller.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/llcpp/vector_view.h"
#include "lib/zx/event.h"
#include "lib/zx/handle.h"
#include "lib/zx/time.h"
#include "zircon/errors.h"
#include "zircon/fidl.h"
#include "zircon/rights.h"
#include "zircon/time.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;
namespace sysinfo = ::llcpp::fuchsia::sysinfo;
namespace sysmem = ::llcpp::fuchsia::sysmem;

namespace {

constexpr uint64_t kEventId = 13;
constexpr uint32_t kCollectionId = 12;
constexpr uint64_t kInvalidId = 34;

class CoreDisplayTest : public zxtest::Test {
 public:
  void SetUp() override;
  void TearDown() override;
  bool IsCaptureSupported() const {
    return (dc_client_->IsCaptureSupported().value().result.response().supported);
  }
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

  std::unique_ptr<fhd::Controller::SyncClient> dc_client_;
  std::unique_ptr<sysinfo::SysInfo::SyncClient> sysinfo_;
  std::unique_ptr<sysmem::Allocator::SyncClient> sysmem_allocator_;
  zx::event client_event_;
  std::unique_ptr<sysmem::BufferCollectionToken::SyncClient> token_;
  std::unique_ptr<sysmem::BufferCollection::SyncClient> collection_;

 private:
  zx::channel device_client_channel_;
  zx::channel dc_client_channel_;
  zx::channel sysinfo_client_channel_;
  zx::channel sysmem_client_channel_;
  fdio_cpp::FdioCaller caller_;
  fbl::Vector<fhd::Info> displays_;
  bool capture_supported_ = false;
};

void CoreDisplayTest::SetUp() {
  zx::channel device_server_channel;
  fbl::unique_fd fd(open("/dev/class/display-controller/000", O_RDWR));
  zx_status_t status = zx::channel::create(0, &device_server_channel, &device_client_channel_);
  ASSERT_OK(status);

  zx::channel dc_server_channel;
  status = zx::channel::create(0, &dc_server_channel, &dc_client_channel_);
  ASSERT_OK(status);

  caller_.reset(std::move(fd));
  auto open_status = fhd::Provider::Call::OpenController(
      caller_.channel(), std::move(device_server_channel), std::move(dc_server_channel));
  ASSERT_TRUE(open_status.ok());
  ASSERT_EQ(ZX_OK, open_status.value().s);

  dc_client_ = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client_channel_));

  bool has_display = false;
  fbl::Vector<fhd::Info> displays_tmp;

  fhd::Controller::EventHandlers handlers{
      .on_displays_changed =
          [&displays_tmp, &has_display](fhd::Controller::OnDisplaysChangedResponse* message) {
            for (unsigned i = 0; i < message->added.count(); i++) {
              displays_tmp.push_back(std::move(message->added[i]));
            }
            has_display = true;
            return ZX_OK;
          },
      .on_vsync = [](fhd::Controller::OnVsyncResponse* message) { return ZX_ERR_NEXT; },
      .on_client_ownership_change =
          [](fhd::Controller::OnClientOwnershipChangeResponse* message) { return ZX_ERR_NEXT; },
      .unknown = []() { return ZX_ERR_STOP; }};
  do {
    fidl::Result result = dc_client_->HandleEvents(handlers);
    ASSERT_TRUE(result.ok() || result.status() == ZX_ERR_NEXT);
  } while (!has_display);

  // get sysmem
  zx::channel sysmem_server_channel;
  status = zx::channel::create(0, &sysmem_server_channel, &sysmem_client_channel_);
  ASSERT_OK(status);
  status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator", sysmem_server_channel.release());
  ASSERT_OK(status);
  sysmem_allocator_ =
      std::make_unique<sysmem::Allocator::SyncClient>(std::move(sysmem_client_channel_));
}

void CoreDisplayTest::TearDown() {
  if (collection_) {
    collection_->Close();
  }
  sysmem_allocator_.reset();
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
  zx::channel token_server;
  zx::channel token_client;
  auto status = zx::channel::create(0, &token_server, &token_client);
  ASSERT_OK(status);

  token_ = std::make_unique<sysmem::BufferCollectionToken::SyncClient>(std::move(token_client));

  // Pass token server to sysmem allocator
  auto alloc_status = sysmem_allocator_->AllocateSharedCollection(std::move(token_server));
  ASSERT_TRUE(alloc_status.ok());
}

void CoreDisplayTest::DuplicateAndImportToken() {
  // Duplicate the token, to be passed to the display controller
  zx::channel token_dup_client;
  zx::channel token_dup_server;
  auto status = zx::channel::create(0, &token_dup_server, &token_dup_client);
  ASSERT_OK(status);
  sysmem::BufferCollectionToken::SyncClient display_token(std::move(token_dup_client));
  auto dup_res = token_->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_dup_server));
  ASSERT_TRUE(dup_res.ok());
  ASSERT_OK(dup_res.status());
  // sync token
  token_->Sync();

  auto import_resp = dc_client_->ImportBufferCollection(
      kCollectionId, std::move(*display_token.mutable_channel()));
  ASSERT_TRUE(import_resp.ok());
  ASSERT_OK(import_resp.value().res);
}

void CoreDisplayTest::SetBufferConstraints() {
  fhd::ImageConfig image_config = {};
  image_config.type = IMAGE_TYPE_CAPTURE;
  auto constraints_resp = dc_client_->SetBufferCollectionConstraints(kCollectionId, image_config);
  ASSERT_TRUE(constraints_resp.ok());
  ASSERT_OK(constraints_resp.value().res);
}

void CoreDisplayTest::FinalizeClientConstraints() {
  // now that we have provided all that's needed to the display controllers, we can
  // return our token, set our own constraints and for allocation
  // Before that, we need to create a channel to communicate with the buffer collection
  zx::channel collection_client;
  zx::channel collection_server;
  auto status = zx::channel::create(0, &collection_server, &collection_client);
  ASSERT_OK(status);
  auto bind_resp = sysmem_allocator_->BindSharedCollection(std::move(*token_->mutable_channel()),
                                                           std::move(collection_server));
  ASSERT_OK(bind_resp.status());

  // token has been returned. Let's set contraints
  sysmem::BufferCollectionConstraints constraints = {};
  constraints.usage.cpu = sysmem::cpuUsageReadOften | sysmem::cpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = false;
  constraints.image_format_constraints_count = 1;
  sysmem::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = sysmem::ColorSpace{
      .type = sysmem::ColorSpaceType::SRGB,
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

  collection_ =
      std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(collection_client));
  auto collection_resp = collection_->SetConstraints(true, constraints);
  ASSERT_OK(collection_resp.status());

  // Token return and constraints set. Wait for allocation
  auto wait_resp = collection_->WaitForBuffersAllocated();
  ASSERT_OK(wait_resp.status());
}

uint64_t CoreDisplayTest::ImportCaptureImage() const {
  // Make the buffer available for capture
  fhd::ImageConfig capture_cfg = {};  // will contain a handle
  auto importcap_resp = dc_client_->ImportImageForCapture(capture_cfg, kCollectionId, 0);
  if (importcap_resp.status() != ZX_OK) {
    return INVALID_ID;
  }
  if (importcap_resp.value().result.is_err()) {
    return importcap_resp.value().result.err();
  }

  return importcap_resp.value().result.response().image_id;
}

zx_status_t CoreDisplayTest::StartCapture(uint64_t id, uint64_t e) const {
  auto startcap_resp = dc_client_->StartCapture(e, id);
  if (!startcap_resp.ok()) {
    return startcap_resp.status();
  }
  if (startcap_resp.value().result.is_err()) {
    return (startcap_resp.value().result.err());
  }
  return ZX_OK;
}

zx_status_t CoreDisplayTest::ReleaseCapture(uint64_t id) const {
  auto releasecap_resp = dc_client_->ReleaseCapture(id);
  if (!releasecap_resp.ok()) {
    return releasecap_resp.status();
  }
  if (releasecap_resp.value().result.is_err()) {
    return (releasecap_resp.value().result.err());
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
  // Setup connects to display controller. Make sure we can't bound again
  fbl::unique_fd fd(open("/dev/class/display-controller/000", O_RDWR));
  zx::channel device_server, device_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  ASSERT_EQ(status, ZX_OK);

  zx::channel dc_server, dc_client;
  status = zx::channel::create(0, &dc_server, &dc_client);
  ASSERT_EQ(status, ZX_OK);

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto open_status = fhd::Provider::Call::OpenController(caller.channel(), std::move(device_server),
                                                         std::move(dc_server));
  EXPECT_TRUE(open_status.ok());
  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, open_status.value().s);
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
  dc_client_->mutable_channel()->reset();
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
  dc_client_->ReleaseBufferCollection(kCollectionId);
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
  dc_client_->ReleaseBufferCollection(kCollectionId);
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
  dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, InvalidStartCaptureId) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, StartCapture(kInvalidId));

  // done. Close sysmem
  dc_client_->ReleaseBufferCollection(kCollectionId);
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
  dc_client_->ReleaseBufferCollection(kCollectionId);
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
  dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, InvalidReleaseCaptureId) {
  if (!IsCaptureSupported()) {
    printf("Test Skipped (capture not supported)\n");
    return;
  }

  CaptureSetup();

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ReleaseCapture(kInvalidId));

  // done. Close sysmem
  dc_client_->ReleaseBufferCollection(kCollectionId);
}

TEST_F(CoreDisplayTest, CaptureNotSupported) {
  if (IsCaptureSupported()) {
    printf("Test Skipped\n");
    return;
  }
  fhd::ImageConfig image_config = {};
  auto import_resp = dc_client_->ImportImageForCapture(image_config, 0, 0);
  EXPECT_TRUE(import_resp.ok());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, import_resp.value().result.err());

  auto start_resp = dc_client_->StartCapture(0, 0);
  EXPECT_TRUE(start_resp.ok());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, start_resp.value().result.err());

  auto release_resp = dc_client_->ReleaseCapture(0);
  EXPECT_TRUE(release_resp.ok());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, release_resp.value().result.err());
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
