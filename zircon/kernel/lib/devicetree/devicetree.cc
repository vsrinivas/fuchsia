// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devicetree/devicetree.h>

namespace devicetree {
namespace {

// https://devicetree-specification.readthedocs.io/en/v0.3/flattened-format.html#header
struct struct_fdt_header {
  uint32_t magic;
  uint32_t totalsize;
  uint32_t off_dt_struct;
  uint32_t off_dt_strings;
  uint32_t off_mem_rsvmap;
  uint32_t version;
  uint32_t last_comp_version;
  uint32_t boot_cpuid_phys;
  uint32_t size_dt_strings;
  uint32_t size_dt_struct;
};

// https://devicetree-specification.readthedocs.io/en/v0.3/flattened-format.html#lexical-structure
struct FdtProperty {
  uint32_t len;
  uint32_t nameoff;
};

struct PropertyBlockContents {
  // The underlying property value.
  ByteView value;
  uint32_t name_offset;
  // The 4-byte aligned tail of the property block view after the value.
  ByteView tail;
};

PropertyBlockContents ReadPropertyBlock(ByteView bytes) {
  ZX_ASSERT(bytes.size() >= sizeof(FdtProperty));
  uint32_t prop_size =
      internal::ReadBigEndianUint32(bytes.substr(offsetof(FdtProperty, len))).value;
  auto [name_offset, block_end] =
      internal::ReadBigEndianUint32(bytes.substr(offsetof(FdtProperty, nameoff)));
  ByteView value = block_end.substr(0, prop_size);
  return {value, name_offset, block_end.substr(internal::StructBlockAlign(prop_size))};
}

}  // namespace

uint64_t PropertyValue::AsUint64() const {
  ZX_ASSERT(bytes_.size() == sizeof(uint64_t));
  auto [high, tail] = internal::ReadBigEndianUint32(bytes_);
  const uint32_t low = internal::ReadBigEndianUint32(tail).value;
  return (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
}

Property Properties::iterator::operator*() const {
  auto [value, name_offset, tail] = ReadPropertyBlock(position_);

  ZX_ASSERT_MSG(name_offset < string_block_.size(),
                "property name does not live in the string block");
  std::string_view name_and_tail = string_block_.substr(name_offset);
  size_t name_end = name_and_tail.find_first_of('\0');
  ZX_ASSERT_MSG(name_end != std::string_view::npos, "property name was not null-terminated");

  return {name_and_tail.substr(0, name_end), value};
}

Properties::iterator& Properties::iterator::operator++() {
  position_ = ReadPropertyBlock(position_).tail;

  // A property block might be followed by no-op tokens; seek past them if so
  // and, space provided, stop just after the next property token.
  while (!position_.empty()) {
    auto [token, tail] = internal::ReadBigEndianUint32(position_);
    position_ = tail;
    switch (token) {
      case internal::FDT_NOP:
        continue;
      case internal::FDT_PROP:
        break;
      default:
        ZX_PANIC("unexpected token in property block: %#x", token);
    };
    break;
  }
  return *this;
}

Devicetree::Devicetree(ByteView blob) {
  ZX_ASSERT(internal::ReadBigEndianUint32(blob).value == internal::kMagic);

  const uint32_t size =
      internal::ReadBigEndianUint32(blob.substr(offsetof(struct_fdt_header, totalsize))).value;
  ZX_ASSERT(size <= blob.size());
  ByteView fdt(blob.data(), size);

  const uint32_t struct_block_offset =
      internal::ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, off_dt_struct))).value;
  const uint32_t struct_block_size =
      internal::ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, size_dt_struct))).value;
  ZX_ASSERT(struct_block_offset < fdt.size());
  ZX_ASSERT(fdt.size() - struct_block_offset >= struct_block_size);

  const uint8_t* struct_block_base = fdt.data() + struct_block_offset;
  ByteView struct_block(struct_block_base, struct_block_size);
  ZX_ASSERT(struct_block_size > 0);
  ZX_ASSERT(internal::ReadBigEndianUint32(struct_block.substr(struct_block_size - sizeof(uint32_t)))
                .value == internal::FDT_END);

  const uint32_t string_block_offset =
      internal::ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, off_dt_strings))).value;
  const uint32_t string_block_size =
      internal::ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, size_dt_strings))).value;
  ZX_ASSERT(string_block_offset <= fdt.size());
  ZX_ASSERT(fdt.size() - struct_block_offset >= struct_block_size);

  const uint8_t* string_block_base = fdt.data() + string_block_offset;
  std::string_view string_block(reinterpret_cast<const char*>(string_block_base),
                                string_block_size);

  fdt_ = fdt;
  struct_block_ = struct_block;
  string_block_ = string_block;
}

ByteView Devicetree::EndOfPropertyBlock(ByteView prop) { return ReadPropertyBlock(prop).tail; }

}  // namespace devicetree
