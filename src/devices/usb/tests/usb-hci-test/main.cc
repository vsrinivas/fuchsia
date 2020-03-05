// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/usb/hcitest/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

static fuchsia::hardware::usb::hcitest::Device_Run_Result results;

static constexpr double BytesToGigabits(double bytes) { return (bytes / 1000 / 1000 / 1000) * 8; }

constexpr double kTestRuntimeInSeconds = 15;

TEST(UsbHciTests, BulkTests) {
  double bytes_received = results.response().results.received_bulk_packets *
                          results.response().results.bulk_packet_size;
  ASSERT_GE(BytesToGigabits(bytes_received) / kTestRuntimeInSeconds, EXPECTED_BITRATE);
}

TEST(UsbHciTests, IsochronousTests) {
  // We should be receiving 120000 packets in 15 seconds (with period of 125 microseconds)
  // but in practice we aren't meeting this with our current driver today.
  // TODO(bbosak): Update this test when the xHCI rewrite gets in.
  // TODO(fxb/45736): Add metrics when infra supports this.
  ASSERT_GE(static_cast<double>(results.response().results.received_isoch_packets),
            EXPECTED_ISOCH_PACKETS);
}

TEST(UsbHciTests, ShortPacketTests) {
  // Asserts that we receive the correct number of bytes when a short packet occurs
  ASSERT_TRUE(results.response().results.got_correct_number_of_bytes_in_short_transfers);
}

int main(int argc, char** argv) {
  fuchsia::hardware::usb::hcitest::DeviceSyncPtr ptr;
  zx::channel chan, remote_end;
  zx::channel::create(0, &chan, &remote_end);
  zx_status_t status = fdio_service_connect("/dev/class/usb-hci-test/000", remote_end.release());
  if (status != ZX_OK) {
    printf("Failed to connect to service due to error %i\n", status);
    return -1;
  }
  ptr.Bind(std::move(chan));
  status = ptr->Run(&results);
  if (status || results.is_err()) {
    printf("Test failed with status %i\n", status);
    return -1;
  }
  zxtest::RunAllTests(argc, argv);
  return 0;
}
