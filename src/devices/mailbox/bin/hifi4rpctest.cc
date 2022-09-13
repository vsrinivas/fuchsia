// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.mailbox/cpp/wire.h>
#include <getopt.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

#include <fbl/unique_fd.h>

#include "src/devices/mailbox/drivers/aml-mailbox/meson_mhu_common.h"

constexpr uint8_t kMboxSystem = 0;
constexpr uint8_t kCmdMboxTest = 6;
constexpr uint8_t kRpcuintSize = 64;
constexpr uint16_t kCmdRpcuintTest = 0x61;
constexpr uint8_t kAocpuResponse[] = "Response AOCPU";

#define MBX_COMPOSE_MSG(mod, func) ((mod << 10) | (func & 0x3FF))
#define MBX_TEST_DEMO MBX_COMPOSE_MSG(kMboxSystem, kCmdMboxTest)
#define MBX_CMD_RPCUINT_TESTA MBX_COMPOSE_MSG(kMboxSystem, kCmdRpcuintTest)

using fuchsia_hardware_mailbox::wire::DeviceReceiveDataResponse;
using fuchsia_hardware_mailbox::wire::MboxRx;
using fuchsia_hardware_mailbox::wire::MboxTx;
using MailboxClient = fidl::WireSyncClient<fuchsia_hardware_mailbox::Device>;

struct MboxUint {
  uint32_t uintcmd;
  uint8_t data[kRpcuintSize];
  uint32_t sumdata;
};

void showUsage(char* arg) {
  fprintf(stderr, "Usage: %s <options>*\n", arg);
  fprintf(stderr, "    %s DEVICE a \n", arg);
  fprintf(stderr, "    %s DEVICE d \n", arg);
  fprintf(stderr, "    %s DEVICE s \n", arg);
}

zx_status_t AocpuTest(MailboxClient client) {
  uint8_t tx_data[kMboxUserLen] = "Aocpu mailbox test";
  MboxTx txmdata = {.cmd = MBX_TEST_DEMO,
                    .tx_buffer = fidl::VectorView<uint8_t>::FromExternal(tx_data, sizeof(tx_data))};

  auto mbox_send_result = client->SendCommand(kMailboxAocpu, txmdata);
  if (!mbox_send_result.ok()) {
    fprintf(stderr, "Failed to mailbox send: %s\n",
            zx_status_get_string(mbox_send_result.status()));
    return mbox_send_result.status();
  }

  fidl::Arena allocator;
  MboxRx rxmdata;
  rxmdata.rx_buffer.Allocate(allocator, sizeof(tx_data));
  auto mbox_receive_result =
      client->ReceiveData(kMailboxAocpu, static_cast<uint8_t>(rxmdata.rx_buffer.count()));
  if (!mbox_receive_result.ok()) {
    fprintf(stderr, "Failed to mailbox receive: %s\n",
            zx_status_get_string(mbox_receive_result.status()));
    return mbox_receive_result.status();
  }

  DeviceReceiveDataResponse* response = mbox_receive_result->value();
  /* According to the agreement, when A5 sends the command MBX_TEST_DEMO to AOCPU through the
   * mailbox, no matter what the specific content of the sent data is, after receiving the command,
   * AOCPU will respond with the data: Response AOCPU. */
  if (memcmp(&response->mdata.rx_buffer[0], kAocpuResponse, sizeof(kAocpuResponse) - 1) == 0) {
    printf("Aocpu testing successfully\n");
    return ZX_OK;
  } else {
    return ZX_ERR_UNAVAILABLE;
  }
}

zx_status_t DspTest(MailboxClient client) {
  MboxUint sendbuf = {
      /* This is an agreement mechanism for ARM and DSP to communicate through Mailbox. When ARM
       * sends the command MBX_CMD_RPCUINT_TESTA and sends the data sendbuf, after the DSP receives
       * the command, it will parse the uintcmd part of the received data sendbuf, and perform
       * different operations according to the value of uintcmd. Depending on the mechanism,
       * possible values for uintcmd are: 0x6, 0x7, 0x89. */
      .uintcmd = 0x6,
      .sumdata = static_cast<uint32_t>(std::size(sendbuf.data)),
  };
  memset(sendbuf.data, 1, std::size(sendbuf.data));
  uint32_t sumdata = sendbuf.sumdata;

  MboxTx txmdata = {.cmd = MBX_CMD_RPCUINT_TESTA,
                    .tx_buffer = fidl::VectorView<uint8_t>::FromExternal(
                        reinterpret_cast<uint8_t*>(&sendbuf), sizeof(MboxUint))};

  auto mbox_send_result = client->SendCommand(kMailboxDsp, txmdata);
  if (!mbox_send_result.ok()) {
    fprintf(stderr, "Failed to mailbox send: %s\n",
            zx_status_get_string(mbox_send_result.status()));
    return mbox_send_result.status();
  }

  fidl::Arena allocator;
  MboxRx rxmdata;
  rxmdata.rx_buffer.Allocate(allocator, sizeof(MboxUint));
  auto mbox_receive_result =
      client->ReceiveData(kMailboxDsp, static_cast<uint8_t>(rxmdata.rx_buffer.count()));
  if (!mbox_receive_result.ok()) {
    fprintf(stderr, "Failed to mailbox receive: %s\n",
            zx_status_get_string(mbox_receive_result.status()));
    return mbox_receive_result.status();
  }

  DeviceReceiveDataResponse* response = mbox_receive_result->value();
  if ((reinterpret_cast<MboxUint*>(&response->mdata.rx_buffer[0]))->sumdata == (sumdata - 1)) {
    printf("Dsp testing successfully!!\n");
    return ZX_OK;
  } else {
    return ZX_ERR_UNAVAILABLE;
  }
}

zx_status_t ScpiTest(MailboxClient client) {
  const char message[] = "SCPI_CMD_HIFI4SUSPEND";
  MboxTx txmdata = {.cmd = MBX_CMD_RPCUINT_TESTA,
                    .tx_buffer = fidl::VectorView<uint8_t>::FromExternal(
                        reinterpret_cast<uint8_t*>(const_cast<char*>(message)), sizeof(message))};

  auto mbox_send_result = client->SendCommand(kMailboxScpi, txmdata);
  if (!mbox_send_result.ok()) {
    fprintf(stderr, "Failed to mailbox send: %s\n",
            zx_status_get_string(mbox_send_result.status()));
    return mbox_send_result.status();
  }

  fidl::Arena allocator;
  MboxRx rxmdata;
  rxmdata.rx_buffer.Allocate(allocator, sizeof(message));
  auto mbox_receive_result =
      client->ReceiveData(kMailboxScpi, static_cast<uint8_t>(rxmdata.rx_buffer.count()));
  if (!mbox_receive_result.ok()) {
    fprintf(stderr, "Failed to mailbox receive: %s\n",
            zx_status_get_string(mbox_receive_result.status()));
    return mbox_receive_result.status();
  }

  DeviceReceiveDataResponse* response = mbox_receive_result->value();
  if (strncmp(reinterpret_cast<char*>(&response->mdata.rx_buffer[0]), message, sizeof(message)) ==
      0) {
    printf("Scpi testing successfully!!\n");
    return ZX_OK;
  } else {
    return ZX_ERR_UNAVAILABLE;
  }
}

int main(int argc, char** argv) {
  if (argc != 3) {
    showUsage(argv[0]);
    return ZX_ERR_INVALID_ARGS;
  }

  int fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
    return ZX_ERR_BAD_PATH;
  }

  fbl::unique_fd device(fd);
  if (!device.is_valid()) {
    fprintf(stderr, "Failed to open mailbox device: %s\n", strerror(errno));
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel svc;
  zx_status_t st = fdio_get_service_handle(device.release(), svc.reset_and_get_address());
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to get service handle: %s\n", zx_status_get_string(st));
    return ZX_ERR_BAD_HANDLE;
  }
  fidl::WireSyncClient<fuchsia_hardware_mailbox::Device> client(std::move(svc));

  zx_status_t status;
  switch (argv[2][0]) {
    case 'a':
      status = AocpuTest(std::move(client));
      if (status != ZX_OK) {
        fprintf(stderr, "AocpuTest failed: %s\n", zx_status_get_string(status));
      }
      break;

    case 'd':
      status = DspTest(std::move(client));
      if (status != ZX_OK) {
        fprintf(stderr, "DspTest failed: %s\n", zx_status_get_string(status));
      }
      break;

    case 's':
      status = ScpiTest(std::move(client));
      if (status != ZX_OK) {
        fprintf(stderr, "ScpiTest failed: %s\n", zx_status_get_string(status));
      }
      break;

    default:
      fprintf(stderr, "%c: unrecognized command\n", argv[2][0]);
      showUsage(argv[0]);
      status = ZX_ERR_INVALID_ARGS;
      break;
  }

  close(fd);
  return status;
}
