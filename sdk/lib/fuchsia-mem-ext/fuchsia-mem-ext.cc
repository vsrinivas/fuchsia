// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fuchsia-mem-ext/fuchsia-mem-ext.h>

namespace fuchsia_mem_ext {

constexpr size_t kDefaultSizeThreshold = 16 * 1024;

namespace internal {

fuchsia::mem::Data CreateInline(cpp20::span<const uint8_t> data) {
  std::vector<uint8_t> bytes(data.size());
  std::memcpy(bytes.data(), data.data(), data.size());
  return fuchsia::mem::Data::WithBytes(std::move(bytes));
}

fuchsia::mem::Data CreateInline(std::vector<uint8_t> data) {
  return fuchsia::mem::Data::WithBytes(std::move(data));
}

template <typename T>
zx::result<fuchsia::mem::Data> Create(T data, size_t size_threshold, cpp17::string_view vmo_name) {
  if (size_threshold > ZX_CHANNEL_MAX_MSG_BYTES) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  if (vmo_name.length() > ZX_MAX_NAME_LEN) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (data.size() <= size_threshold) {
    return zx::ok(CreateInline(std::move(data)));
  }

  zx::vmo vmo;
  uint32_t options = 0;
  zx_status_t status = zx::vmo::create(data.size(), options, &vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = vmo.write(data.data(), 0u, data.size());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if (!vmo_name.empty()) {
    status = vmo.set_property(ZX_PROP_NAME, vmo_name.data(), vmo_name.length());
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }

  return zx::ok(fuchsia::mem::Data::WithBuffer({std::move(vmo), data.size()}));
}

}  // namespace internal

zx::result<fuchsia::mem::Data> CreateWithData(cpp20::span<const uint8_t> data,
                                              cpp17::string_view vmo_name) {
  return CreateWithData(data, kDefaultSizeThreshold, vmo_name);
}

zx::result<fuchsia::mem::Data> CreateWithData(std::vector<uint8_t> data,
                                              cpp17::string_view vmo_name) {
  return CreateWithData(std::move(data), kDefaultSizeThreshold, vmo_name);
}

zx::result<fuchsia::mem::Data> CreateWithData(cpp20::span<const uint8_t> data,
                                              size_t size_threshold, cpp17::string_view vmo_name) {
  return internal::Create(data, size_threshold, vmo_name);
}

zx::result<fuchsia::mem::Data> CreateWithData(std::vector<uint8_t> data, size_t size_threshold,
                                              cpp17::string_view vmo_name) {
  return internal::Create(std::move(data), size_threshold, vmo_name);
}

zx::result<std::vector<uint8_t>> ExtractData(fuchsia::mem::Data data) {
  switch (data.Which()) {
    case fuchsia::mem::Data::Tag::kBytes: {
      return zx::ok(std::move(data).bytes());
    }
    case fuchsia::mem::Data::Tag::kBuffer: {
      const size_t data_size = data.buffer().size;
      std::vector<uint8_t> ret(data_size);
      zx_status_t status = data.buffer().vmo.read(ret.data(), 0u, data_size);
      if (status != ZX_OK) {
        return zx::error(status);
      }
      return zx::ok(std::move(ret));
    }
    case fuchsia::mem::Data::kUnknown:
    case fuchsia::mem::Data::Invalid:
      return zx::error(ZX_ERR_BAD_STATE);
  }
}

}  // namespace fuchsia_mem_ext
