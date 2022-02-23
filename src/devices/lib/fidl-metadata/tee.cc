// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/lib/fidl-metadata/tee.h"

#include <fidl/fuchsia.hardware.tee/cpp/wire.h>

namespace fidl_metadata::tee {
zx::status<std::vector<uint8_t>> TeeMetadataToFidl(
    uint32_t default_thread_count, cpp20::span<const CustomThreadConfig> thread_config) {
  fidl::Arena allocator;

  fuchsia_hardware_tee::wire::TeeMetadata metadata(allocator);
  metadata.set_default_thread_count(default_thread_count);

  fidl::VectorView<fuchsia_hardware_tee::wire::CustomThreadConfig> thr_config(allocator,
                                                                              thread_config.size());

  for (size_t i = 0; i < thread_config.size(); i++) {
    auto& thr = thr_config[i];
    auto& src_thr = thread_config[i];
    thr.Allocate(allocator);

    thr.set_role(allocator, fidl::StringView(allocator, src_thr.role));
    thr.set_count(src_thr.count);
    fidl::VectorView<::fuchsia_tee::wire::Uuid> apps(allocator, src_thr.trusted_apps.size());
    for (size_t j = 0; j < src_thr.trusted_apps.size(); j++) {
      auto& app = apps[j];
      auto& src_app = src_thr.trusted_apps[j];

      app.time_low = src_app.time_low;
      app.time_mid = src_app.time_mid;
      app.time_hi_and_version = src_app.time_hi_and_version;
      ::memcpy(app.clock_seq_and_node.data(), src_app.clock_seq_and_node, 8);
    }

    thr.set_trusted_apps(allocator, apps);
  }

  metadata.set_custom_threads(allocator, thr_config);

  fidl::unstable::OwnedEncodedMessage<fuchsia_hardware_tee::wire::TeeMetadata> encoded(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  if (!encoded.ok()) {
    return zx::error(encoded.status());
  }

  auto message = encoded.GetOutgoingMessage().CopyBytes();
  std::vector<uint8_t> result(message.data(), message.data() + message.size());
  return zx::ok(std::move(result));
}
}  // namespace fidl_metadata::tee
