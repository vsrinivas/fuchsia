// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_DEBUGDATA_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_DEBUGDATA_H_

#include <string_view>

#include "item-base.h"

namespace boot_shim {

class DebugdataItem : public ItemBase {
 public:
  size_t size_bytes() const { return ItemSize(payload_size_bytes()); }

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi);

  void Init(std::string_view sink_name, std::string_view vmo_name,
            std::string_view vmo_name_suffix = "") {
    sink_name_ = sink_name;
    vmo_name_ = vmo_name;
    vmo_name_suffix_ = vmo_name_suffix;
  }

  WritableBytes contents() const {
    if (contents_) {
      return {contents_, content_size_};
    }
    return {};
  }

  DebugdataItem& set_content_size(size_t size) {
    content_size_ = size;
    return *this;
  }

  DebugdataItem& set_log(std::string_view log) {
    log_ = log;
    return *this;
  }

 private:
  size_t payload_size_bytes() const;

  std::string_view sink_name_, vmo_name_, vmo_name_suffix_, log_;
  std::byte* contents_ = nullptr;
  size_t content_size_ = 0;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_DEBUGDATA_H_
