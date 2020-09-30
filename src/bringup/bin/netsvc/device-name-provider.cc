// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/svc/outgoing.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <cerrno>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <inet6/netifc-discover.h>

#include "args.h"
#include "eff_short_wordlist_1.h"

#define APPEND_WORD(NUM, SEP)                          \
  word = dictionary[(NUM) % DICEWARE_DICTIONARY_SIZE]; \
  memcpy(dest, word, strlen(word));                    \
  dest += strlen(word);                                \
  *dest = SEP;                                         \
  dest++;

void device_id_get(unsigned char mac[6], char out[HOST_NAME_MAX]) {
  const char* word;
  char* dest = out;
  APPEND_WORD(mac[0] | ((mac[4] << 8) & 0xF00), '-');
  APPEND_WORD(mac[1] | ((mac[5] << 8) & 0xF00), '-');
  APPEND_WORD(mac[2] | ((mac[4] << 4) & 0xF00), '-');
  APPEND_WORD(mac[3] | ((mac[5] << 4) & 0xF00), 0);
}

class DeviceNameProviderServer final : public llcpp::fuchsia::device::NameProvider::Interface {
  const char* name;
  const size_t size;

 public:
  DeviceNameProviderServer(const char* device_name, size_t size) : name(device_name), size(size) {}
  void GetDeviceName(GetDeviceNameCompleter::Sync& completer) override {
    completer.ReplySuccess(fidl::unowned_str(name, size));
  }
};

int main(int argc, char** argv) {
  const char* errmsg = nullptr;
  const char* interface = nullptr;
  const char* nodename = nullptr;
  const char* ethdir = "/dev/class/ethernet";
  char device_name[HOST_NAME_MAX];
  int err = parse_device_name_provider_args(argc, argv, &errmsg, &interface, &nodename, &ethdir);
  if (err) {
    printf("device-name-provider: FATAL: parse_device_name_provider_args(_) = %d; %s\n", err,
           errmsg);
    return err;
  }

  if (nodename != nullptr) {
    strlcpy(device_name, nodename, sizeof(device_name));
  } else {
    uint8_t mac[6];
    if ((err = netifc_discover(ethdir, interface, nullptr, mac))) {
      strlcpy(device_name, llcpp::fuchsia::device::DEFAULT_DEVICE_NAME, sizeof(device_name));
      printf(
          "device-name-provider: using default name \"%s\": netifc_discover(\"%s\", ...) = %d: "
          "%s\n",
          device_name, ethdir, err, strerror(errno));
    } else {
      device_id_get(mac, device_name);
      printf("device-name-provider: generated device name: %s\n", device_name);
    }
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  async_dispatcher_t* dispatcher = loop.dispatcher();
  if (dispatcher == nullptr) {
    printf("device-name-provider: FATAL: loop.dispatcher() = nullptr\n");
    return -1;
  }

  svc::Outgoing outgoing(dispatcher);
  zx_status_t status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("device-name-provider: FATAL: outgoing.ServeFromStartupInfo() = %s\n",
           zx_status_get_string(status));
    return -1;
  }

  DeviceNameProviderServer server(device_name, strnlen(device_name, sizeof(device_name)));

  outgoing.svc_dir()->AddEntry(
      llcpp::fuchsia::device::NameProvider::Name,
      fbl::AdoptRef(new fs::Service([dispatcher, server](zx::channel svc_request) mutable {
        zx_status_t status =
            fidl::BindSingleInFlightOnly(dispatcher, std::move(svc_request), &server);
        if (status != ZX_OK) {
          printf("device-name-provider: fidl::BindSingleInFlightOnly(_) = %s\n",
                 zx_status_get_string(status));
        }
        return status;
      })));

  status = loop.Run();
  printf("device-name-provider: loop.Run() = %s\n", zx_status_get_string(status));
  return status;
}
