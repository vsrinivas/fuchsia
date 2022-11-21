// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.usb.hcitest/cpp/wire.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <memory>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

static fuchsia_hardware_usb_hcitest::wire::DeviceRunResponse response;

static constexpr double BytesToGigabits(double bytes) { return (bytes / 1000 / 1000 / 1000) * 8; }

constexpr double kTestRuntimeInSeconds = 15;

TEST(UsbHciTests, BulkTests) {
  double bytes_received = static_cast<double>(response.results.received_bulk_packets) *
                          static_cast<double>(response.results.bulk_packet_size);

  ASSERT_GE(BytesToGigabits(bytes_received) / kTestRuntimeInSeconds, EXPECTED_BITRATE);
}

TEST(UsbHciTests, IsochronousTests) {
  // We should be receiving 120000 packets in 15 seconds (with period of 125 microseconds)
  // but in practice we aren't meeting this with our current driver today.
  // TODO(bbosak): Update this test when the xHCI rewrite gets in.
  // TODO(fxbug.dev/45736): Add metrics when infra supports this.
  ASSERT_GE(static_cast<double>(response.results.received_isoch_packets), EXPECTED_ISOCH_PACKETS);
}

TEST(UsbHciTests, ShortPacketTests) {
  // Asserts that we receive the correct number of bytes when a short packet occurs
  ASSERT_TRUE(response.results.got_correct_number_of_bytes_in_short_transfers);
}

int main(int argc, char** argv) {
  std::vector<std::string> paths;
  for (auto const& entry : std::filesystem::directory_iterator{"/dev/class/usb-hci-test"}) {
    paths.push_back(entry.path());
  }
  if (paths.empty()) {
    printf("failed to find usb-hci-test device\n");
    return -1;
  }
  if (paths.size() > 1) {
    printf("found %zu usb-hci-test devices\n", paths.size());
    return -1;
  }
  std::string_view path = paths.front();
  zx::result client_end = component::Connect<fuchsia_hardware_usb_hcitest::Device>(path);
  if (!client_end.is_ok()) {
    printf("failed to connect to service due to error %s\n", client_end.status_string());
    return -1;
  }
  const fidl::WireResult wire_result = fidl::WireCall(client_end.value())->Run();
  if (!wire_result.ok()) {
    printf("test failed: %s\n", wire_result.FormatDescription().c_str());
    return -1;
  }
  const fit::result wire_response = wire_result.value();
  if (wire_response.is_error()) {
    printf("test failed: %s\n", zx_status_get_string(wire_response.error_value()));
    return -1;
  }
  response = *wire_response.value();
  zxtest::RunAllTests(argc, argv);
  return 0;
}
