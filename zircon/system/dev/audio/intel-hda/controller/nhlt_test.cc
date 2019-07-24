// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nhlt.h"

#include <zircon/errors.h>

#include <vector>

#include <intel-hda/utils/nhlt.h>
#include <zxtest/zxtest.h>

namespace audio::intel_hda {
namespace {

// Push the raw bytes of the given object into the given std::vector<uint8_t>.
template <typename T>
void PushBytes(std::vector<uint8_t>* buffer, const T* object) {
  const auto* object_bytes = reinterpret_cast<const uint8_t*>(object);
  std::copy(object_bytes, object_bytes + sizeof(T), std::back_inserter(*buffer));
}

template <typename T>
fbl::Array<T> ToArray(const std::vector<T>& vec) {
  T* data = new (std::nothrow) T[vec.size()];
  ZX_ASSERT(data != nullptr);
  for (size_t i = 0; i < vec.size(); i++) {
    data[i] = vec[i];
  }
  return fbl::Array<T>(data, vec.size());
}

std::vector<uint8_t> SampleEndpoint() {
  std::vector<uint8_t> data;

  // Write out an endpoint.
  nhlt_descriptor_t endpoint = {
      /*length=*/(sizeof(nhlt_descriptor_t) + sizeof(formats_config_t) + sizeof(format_config_t)),
      /*link_type=*/NHLT_LINK_TYPE_SSP,
      /*instance_id=*/1,
      /*vendor_id=*/2,
      /*device_id=*/3,
      /*revision_id=*/4,
      /*subsystem_id=*/5,
      /*device_type=*/6,
      /*direction=*/NHLT_DIRECTION_RENDER,
      /*virtual_bus_id=*/7,
      /*config=*/{/*capabilities_size=*/0},
  };
  PushBytes(&data, &endpoint);

  formats_config_t formats = {
      /*format_config_count=*/1,
  };
  PushBytes(&data, &formats);

  format_config_t format = {
      /*format_tag=*/0,
      /*n_channels=*/0,
      /*n_samples_per_sec=*/0,
      /*n_avg_bytes_per_sec=*/0,
      /*n_block_align=*/0,
      /*bits_per_sample=*/0,
      /*cb_size=*/0,
      /*valid_bits_per_sample=*/0,
      /*channel_mask=*/0,
      /*subformat_guid=*/{},
      /*config=*/{/*capabilities_size=*/0},
  };
  PushBytes(&data, &format);

  return data;
}

std::vector<uint8_t> SampleNHLT() {
  std::vector<uint8_t> data;

  // Create endpoint.
  std::vector<uint8_t> endpoint = SampleEndpoint();

  // Write out header.
  nhlt_table_t nhlt = {
      /*header=*/{
          /*signature=*/{'N', 'H', 'L', 'T'},
          /*length=*/static_cast<uint32_t>(sizeof(nhlt_table_t) + endpoint.size()),
          /*revision=*/5,
          /*checksum=*/0,  // Invalid, but we don't check.
          /*oem_id=*/{'O', 'E', 'M'},
          /*oem_table_id=*/{'T', 'A', 'B', 'L', 'E'},
          /*oem_revision=*/0,
          /*asl_compiler_id=*/{'C', 'O', 'M', 'P'},
          /*asl_compiler_revision=*/0,
      },
      /*endpoint_desc_count=*/1,
  };
  PushBytes(&data, &nhlt);

  // Append endpoint.
  std::copy(endpoint.begin(), endpoint.end(), std::back_inserter(data));

  return data;
}

TEST(Nhlt, DefaultInitializer) {
  Nhlt x{};
  EXPECT_TRUE(x.i2s_configs().is_empty());
}

TEST(Nhlt, ParseEmpty) {
  fbl::Array<uint8_t> empty{};
  EXPECT_FALSE(Nhlt::FromBuffer(std::move(empty)).ok());
}

TEST(Nhlt, ParseSimple) {
  std::vector<uint8_t> data = SampleNHLT();
  StatusOr<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(ToArray(data));
  ASSERT_OK(nhlt.status().code());
  ASSERT_EQ(nhlt.ValueOrDie()->i2s_configs().size(), 1);
}

}  // namespace
}  // namespace audio::intel_hda
