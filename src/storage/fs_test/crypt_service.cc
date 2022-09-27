// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/crypt_service.h"

#include <fidl/fuchsia.fxfs/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/sys/component/cpp/service_client.h>

#include "sdk/lib/syslog/cpp/macros.h"

namespace fs_test {

zx::status<> SetUpCryptWithRandomKeys(
    fidl::UnownedClientEnd<fuchsia_io::Directory> service_directory) {
  fidl::WireSyncClient<fuchsia_fxfs::CryptManagement> client;
  if (auto management_service_or =
          component::ConnectAt<fuchsia_fxfs::CryptManagement>(service_directory);
      management_service_or.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to crypt management service: "
                   << management_service_or.status_string();
    return management_service_or.take_error();
  } else {
    client = fidl::WireSyncClient(*std::move(management_service_or));
  }
  unsigned char key[32];
  zx_cprng_draw(key, sizeof(key));
  if (auto result = client->AddWrappingKey(0, fidl::VectorView<unsigned char>::FromExternal(key));
      !result.ok()) {
    FX_LOGS(ERROR) << "Failed to add wrapping key: " << result.status_string();
    return zx::error(result.status());
  }
  zx_cprng_draw(key, sizeof(key));
  if (auto result = client->AddWrappingKey(1, fidl::VectorView<unsigned char>::FromExternal(key));
      !result.ok()) {
    FX_LOGS(ERROR) << "Failed to add wrapping key: " << result.status_string();
    return zx::error(result.status());
  }
  if (auto result = client->SetActiveKey(fuchsia_fxfs::wire::KeyPurpose::kData, 0); !result.ok()) {
    FX_LOGS(ERROR) << "Failed to set active data key: " << result.status_string();
    return zx::error(result.status());
  }
  if (auto result = client->SetActiveKey(fuchsia_fxfs::wire::KeyPurpose::kMetadata, 1);
      !result.ok()) {
    FX_LOGS(ERROR) << "Failed to set active data key: " << result.status_string();
    return zx::error(result.status());
  }
  return zx::ok();
}

zx::status<zx::channel> GetCryptService() {
  static bool initialized = false;
  if (!initialized) {
    auto service_endpoints_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (service_endpoints_or.is_error()) {
      FX_LOGS(ERROR) << "Unable to create endpoints: " << service_endpoints_or.status_string();
      return service_endpoints_or.take_error();
    }

    if (zx_status_t status =
            fdio_open("/svc",
                      static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                            fuchsia_io::wire::OpenFlags::kRightWritable),
                      service_endpoints_or->server.TakeChannel().release());
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to open /svc: " << zx_status_get_string(status);
      return zx::error(status);
    }

    if (auto status = SetUpCryptWithRandomKeys(service_endpoints_or->client); status.is_error()) {
      return status.take_error();
    }

    initialized = true;
  }

  if (auto crypt_service_or = component::Connect<fuchsia_fxfs::Crypt>();
      crypt_service_or.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to the crypt service: "
                   << crypt_service_or.status_string();
    return crypt_service_or.take_error();
  } else {
    return zx::ok(crypt_service_or->TakeChannel());
  }
}

}  // namespace fs_test

extern "C" {

// Exported for Rust
zx_status_t get_crypt_service(zx_handle_t* handle) {
  if (auto channel = fs_test::GetCryptService(); channel.is_error()) {
    return channel.error_value();
  } else {
    *handle = channel->release();
    return ZX_OK;
  }
}

}  // extern
