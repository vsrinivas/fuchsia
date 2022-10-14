// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.mediacodec/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "lib/stdcompat/string_view.h"

namespace amlogic_decoder {
namespace test {

// TODO(fxbug.dev/104928) Remove the calls to fuchsia.device/Controller once upgraded to DFv2
class TestDeviceBase {
 public:
  static zx::status<TestDeviceBase> CreateFromFileName(std::string_view path) {
    zx::status client_end =
        component::Connect<fuchsia_device::Controller>(std::string(path).c_str());
    if (client_end.is_error()) {
      return client_end.take_error();
    }
    return zx::ok(TestDeviceBase(std::move(client_end.value())));
  }

  static zx::status<TestDeviceBase> CreateFromTopologicalPathSuffix(const fbl::unique_fd& fd,
                                                                    std::string_view suffix) {
    std::optional<TestDeviceBase> out;
    std::pair<std::reference_wrapper<decltype(out)>, std::string_view> pair{out, suffix};
    zx_status_t status = fdio_watch_directory(
        fd.get(),
        [](int dirfd, int event, const char* fn, void* cookie) {
          if (std::string_view{fn} == ".") {
            return ZX_OK;
          }

          auto& [out, suffix] = *static_cast<std::add_pointer<decltype(pair)>::type>(cookie);

          fdio_cpp::UnownedFdioCaller caller(dirfd);
          zx::status client_end =
              component::ConnectAt<fuchsia_device::Controller>(caller.directory(), fn);
          if (client_end.is_error()) {
            return client_end.error_value();
          }
          const fidl::WireResult result = fidl::WireCall(client_end.value())->GetTopologicalPath();
          if (!result.ok()) {
            return result.status();
          }
          const fit::result response = result.value();
          if (response.is_error()) {
            return response.error_value();
          }
          std::string_view path = (*response.value()).path.get();
          if (cpp20::ends_with(path, suffix)) {
            out.get() = TestDeviceBase(std::move(client_end.value()));
            return ZX_ERR_STOP;
          }
          return ZX_OK;
        },
        zx::time::infinite().get(), &pair);
    if (status == ZX_OK) {
      return zx::error(ZX_ERR_INTERNAL);
    }
    if (status != ZX_ERR_STOP) {
      return zx::error(status);
    }
    if (!out.has_value()) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    return zx::ok(std::move(out.value()));
  }

  // Get a channel to the parent device, so we can rebind the driver to it. This
  // can require sandbox access to /dev/sys.
  zx::status<TestDeviceBase> GetParentDevice() const {
    const fidl::WireResult result = fidl::WireCall(client_end_)->GetTopologicalPath();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    const fit::result response = result.value();
    if (response.is_error()) {
      return zx::error(response.error_value());
    }
    std::string_view path = (*response.value()).path.get();
    std::cout << "device path: " << path << std::endl;
    // Remove everything after the final slash.
    if (const size_t index = path.rfind('/'); index != std::string::npos) {
      path = path.substr(0, index);
    }
    std::cout << "parent device path: " << path << std::endl;
    return CreateFromFileName(path);
  }

  zx::status<> UnbindChildren() const {
    const fidl::WireResult result = fidl::WireCall(client_end_)->UnbindChildren();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    const fit::result response = result.value();
    if (response.is_error()) {
      return zx::error(response.error_value());
    }
    return zx::ok();
  }

  zx::status<> BindDriver(std::string_view path) const {
    // Rebinding the device immediately after unbinding it sometimes causes the new device to be
    // created before the old one is released, which can cause problems since the old device can
    // hold onto interrupts and other resources. Delay recreation to make that less likely.
    // TODO(fxbug.dev/39852): Remove when the driver framework bug is fixed.
    constexpr uint32_t kRecreateDelayMs = 1000;
    zx::nanosleep(zx::deadline_after(zx::msec(kRecreateDelayMs)));

    constexpr uint32_t kMaxRetryCount = 5;
    uint32_t retry_count = 0;
    while (retry_count++ < kMaxRetryCount) {
      // Don't use rebind because we need the recreate delay above. Also, the parent device may have
      // other children that shouldn't be unbound.
      const fidl::WireResult result =
          fidl::WireCall(client_end_)->Bind(fidl::StringView::FromExternal(path));
      if (!result.ok()) {
        return zx::error(result.status());
      }
      const fit::result response = result.value();
      if (response.is_error()) {
        if (zx_status_t status = response.error_value(); status != ZX_ERR_ALREADY_BOUND) {
          return zx::error(status);
        }
        zx::nanosleep(zx::deadline_after(zx::msec(10)));
        continue;
      }
      return zx::ok();
    }
    return zx::error(ZX_ERR_TIMED_OUT);
  }

  zx::status<> Unbind() const {
    const fidl::WireResult result = fidl::WireCall(client_end_)->ScheduleUnbind();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    const fit::result response = result.value();
    if (response.is_error()) {
      return zx::error(response.error_value());
    }
    return zx::ok();
  }

  fidl::UnownedClientEnd<fuchsia_hardware_mediacodec::Tester> tester() const {
    return fidl::UnownedClientEnd<fuchsia_hardware_mediacodec::Tester>(
        client_end_.channel().borrow());
  }

 private:
  explicit TestDeviceBase(fidl::ClientEnd<fuchsia_device::Controller> client_end)
      : client_end_(std::move(client_end)) {}

  fidl::ClientEnd<fuchsia_device::Controller> client_end_;
};

constexpr char kMediaCodecPath[] = "/dev/class/media-codec";
constexpr std::string_view kTopologicalPathSuffix = "/aml-video/amlogic_video";
constexpr std::string_view kTestTopologicalPathSuffix = "/aml-video/test_amlogic_video";

// Requires the driver to be in the system image, so disabled by default.
TEST(TestRunner, DISABLED_RunTests) {
  fbl::unique_fd media_codec(open(kMediaCodecPath, O_RDONLY));
  ASSERT_TRUE(media_codec.is_valid(), "%s", strerror(errno));

  zx::status test_device1 =
      TestDeviceBase::CreateFromTopologicalPathSuffix(media_codec, kTopologicalPathSuffix);
  ASSERT_OK(test_device1.status_value());

  zx::status parent_device = test_device1.value().GetParentDevice();
  ASSERT_OK(parent_device.status_value());

  ASSERT_OK(parent_device.value().UnbindChildren().status_value());
  ASSERT_OK(parent_device.value()
                .BindDriver("/system/driver/amlogic_video_decoder_test.so")
                .status_value());

  zx::status test_device2 =
      TestDeviceBase::CreateFromTopologicalPathSuffix(media_codec, kTestTopologicalPathSuffix);
  ASSERT_OK(test_device2.status_value());

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  ASSERT_OK(fdio_open("/tmp", static_cast<uint32_t>(fuchsia_io::OpenFlags::kRightWritable),
                      server.release()));
  ASSERT_OK(fidl::WireCall(test_device2.value().tester())
                ->SetOutputDirectoryHandle(std::move(client))
                .status());

  const fidl::WireResult result = fidl::WireCall(test_device2.value().tester())->RunTests();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.result);

  // UnbindChildren seems to block for some reason.
  ASSERT_OK(test_device2.value().Unbind().status_value());

  // Try to rebind the correct driver.
  ASSERT_OK(parent_device.value().BindDriver({}).status_value());
}

// Test that unbinding and rebinding the driver works.
TEST(TestRunner, Rebind) {
  fbl::unique_fd media_codec(open(kMediaCodecPath, O_RDONLY));
  ASSERT_TRUE(media_codec.is_valid(), "%s", strerror(errno));

  zx::status test_device1 =
      TestDeviceBase::CreateFromTopologicalPathSuffix(media_codec, kTopologicalPathSuffix);
  ASSERT_OK(test_device1.status_value());

  zx::status parent_device = test_device1.value().GetParentDevice();
  ASSERT_OK(parent_device.status_value());

  ASSERT_OK(parent_device.value().UnbindChildren().status_value());

  // Use autobind to bind same driver.
  ASSERT_OK(parent_device.value().BindDriver({}).status_value());
}

}  // namespace test
}  // namespace amlogic_decoder
