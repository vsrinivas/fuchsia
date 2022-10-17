// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/svc/outgoing.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/status.h>

#include <cerrno>

#include "args.h"
#include "name_tokens.h"
#include "src/bringup/bin/netsvc/netifc-discover.h"
#include "src/lib/storage/vfs/cpp/service.h"

// Copies a word from the wordlist starting at |dest| and then adds |sep| at the end.
// Returns a pointer to the character after the separator.
char* append_word(char* dest, uint16_t num, char sep) {
  const char* word = dictionary[num % TOKEN_DICTIONARY_SIZE];
  memcpy(dest, word, strlen(word));
  dest += strlen(word);
  *dest = sep;
  dest++;
  return dest;
}

void device_id_get_words(const unsigned char mac[6], char out[HOST_NAME_MAX]) {
  char* dest = out;
  dest = append_word(dest, static_cast<uint16_t>(mac[0] | ((mac[4] << 8) & 0xF00)), '-');
  dest = append_word(dest, static_cast<uint16_t>(mac[1] | ((mac[5] << 8) & 0xF00)), '-');
  dest = append_word(dest, static_cast<uint16_t>(mac[2] | ((mac[4] << 4) & 0xF00)), '-');
  dest = append_word(dest, static_cast<uint16_t>(mac[3] | ((mac[5] << 4) & 0xF00)), 0);
}

const char hex_chars[17] = "0123456789abcdef";

// Copies 4 hex characters of hex value of the bits of |num|.
// Then writes |sep| to the character after.
// Returns a pointer to the character after the separator.
char* append_hex(char* dest, uint16_t num, char sep) {
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t left = num >> ((3 - i) * 4);
    *dest = hex_chars[left & 0x0F];
    dest++;
  }
  *dest = sep;
  dest++;
  return dest;
}

#define PREFIX_LEN 9
const char mac_prefix[PREFIX_LEN] = "fuchsia-";

void device_id_get_mac(const unsigned char mac[6], char out[HOST_NAME_MAX]) {
  char* dest = out;
  // Prepend with 'fs-'
  // Prepended with mac_prefix
  for (uint8_t i = 0; i < PREFIX_LEN; i++) {
    dest[i] = mac_prefix[i];
  }
  dest = dest + PREFIX_LEN - 1;
  dest = append_hex(dest, static_cast<uint16_t>((mac[0] << 8) | mac[1]), '-');
  dest = append_hex(dest, static_cast<uint16_t>((mac[2] << 8) | mac[3]), '-');
  dest = append_hex(dest, static_cast<uint16_t>((mac[4] << 8) | mac[5]), 0);
}

void device_id_get(const unsigned char mac[6], char out[HOST_NAME_MAX], uint32_t generation) {
  if (generation == 1) {
    device_id_get_mac(mac, out);
  } else {  // Style 0
    device_id_get_words(mac, out);
  }
}

class DeviceNameProviderServer final : public fidl::WireServer<fuchsia_device::NameProvider> {
  const char* name;
  const size_t size;

 public:
  DeviceNameProviderServer(const char* device_name, size_t size) : name(device_name), size(size) {}
  void GetDeviceName(GetDeviceNameCompleter::Sync& completer) override {
    completer.ReplySuccess(fidl::StringView::FromExternal(name, size));
  }
};

int main(int argc, char** argv) {
  fbl::unique_fd svc_root(open("/svc", O_RDWR | O_DIRECTORY));
  fdio_cpp::UnownedFdioCaller caller(svc_root.get());

  DeviceNameProviderArgs args;
  const char* errmsg = nullptr;
  int err = ParseArgs(argc, argv, caller.borrow_as<fuchsia_io::Directory>(), &errmsg, &args);
  if (err) {
    printf("device-name-provider: FATAL: ParseArgs(_) = %d; %s\n", err, errmsg);
    return err;
  }

  char device_name[HOST_NAME_MAX];
  if (!args.nodename.empty()) {
    strlcpy(device_name, args.nodename.c_str(), sizeof(device_name));
  } else {
    zx::result status = netifc_discover(args.devdir, args.interface);
    if (status.is_error()) {
      strlcpy(device_name, fuchsia_device::wire::kDefaultDeviceName, sizeof(device_name));
      printf("device-name-provider: using default name \"%s\": netifc_discover(\"%s\", ...) = %s\n",
             device_name, args.devdir.c_str(), status.status_string());
    } else {
      const auto& [dev, mac] = status.value();
      device_id_get(mac.x, device_name, args.namegen);
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
      fidl::DiscoverableProtocolName<fuchsia_device::NameProvider>,
      fbl::MakeRefCounted<fs::Service>(
          [dispatcher, server](fidl::ServerEnd<fuchsia_device::NameProvider> server_end) mutable {
            zx_status_t status =
                fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), &server);
            if (status != ZX_OK) {
              printf("device-name-provider: fidl::BindSingleInFlightOnly(_) = %s\n",
                     zx_status_get_string(status));
            }
            return status;
          }));

  status = loop.Run();
  printf("device-name-provider: loop.Run() = %s\n", zx_status_get_string(status));
  return status;
}
