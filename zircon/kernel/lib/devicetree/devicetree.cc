// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/devicetree/devicetree.h>
#include <lib/zircon-internal/align.h>
#include <zircon/assert.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace devicetree {
namespace {

constexpr uint32_t kMagic = 0xd00dfeed;
// The structure block tokens, named as in the spec for clarity.
constexpr uint32_t FDT_BEGIN_NODE = 0x00000001;
constexpr uint32_t FDT_END_NODE = 0x00000002;
constexpr uint32_t FDT_PROP = 0x00000003;
constexpr uint32_t FDT_NOP = 0x00000004;
constexpr uint32_t FDT_END = 0x00000009;

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

// Structure block tokens are 4-byte aligned.
inline size_t StructBlockAlign(size_t x) { return ZX_ALIGN(x, sizeof(uint32_t)); }

struct ReadBigEndianUint32Result {
  uint32_t value;
  ByteView tail;
};

inline ReadBigEndianUint32Result ReadBigEndianUint32(ByteView bytes) {
  ZX_ASSERT(bytes.size() >= sizeof(uint32_t));
  return {
      (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
          (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]),
      bytes.substr(sizeof(uint32_t)),
  };
}

struct ReadBigEndianUint64Result {
  uint64_t value;
  ByteView tail;
};

ReadBigEndianUint64Result ReadBigEndianUint64(ByteView bytes) {
  auto [high, tail] = ReadBigEndianUint32(bytes);
  auto [low, rest] = ReadBigEndianUint32(tail);
  return {
      (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low),
      rest,
  };
}

struct PropertyBlockContents {
  // The underlying property value.
  ByteView value;
  uint32_t name_offset;
  // The 4-byte aligned tail of the property block view after the value.
  ByteView tail;
};

PropertyBlockContents ReadPropertyBlock(ByteView bytes) {
  ZX_ASSERT(bytes.size() >= sizeof(FdtProperty));
  uint32_t prop_size = ReadBigEndianUint32(bytes.substr(offsetof(FdtProperty, len))).value;
  auto [name_offset, block_end] = ReadBigEndianUint32(bytes.substr(offsetof(FdtProperty, nameoff)));
  ByteView value = block_end.substr(0, prop_size);
  return {value, name_offset, block_end.substr(StructBlockAlign(prop_size))};
}

}  // namespace

std::optional<uint32_t> PropertyValue::AsUint32() const {
  if (bytes_.size() != sizeof(uint32_t)) {
    return std::nullopt;
  }
  return ReadBigEndianUint32(bytes_).value;
}

std::optional<uint64_t> PropertyValue::AsUint64() const {
  if (bytes_.size() != sizeof(uint64_t)) {
    return std::nullopt;
  }
  return ReadBigEndianUint64(bytes_).value;
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
    auto [token, tail] = ReadBigEndianUint32(position_);
    position_ = tail;
    switch (token) {
      case FDT_NOP:
        continue;
      case FDT_PROP:
        break;
      default:
        ZX_PANIC("unexpected token in property block: %#x", token);
    };
    break;
  }
  return *this;
}

Devicetree::Devicetree(ByteView blob) {
  ZX_ASSERT(ReadBigEndianUint32(blob).value == kMagic);

  const uint32_t size =
      ReadBigEndianUint32(blob.substr(offsetof(struct_fdt_header, totalsize))).value;
  ZX_ASSERT(size <= blob.size());
  ByteView fdt(blob.data(), size);

  const uint32_t struct_block_offset =
      ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, off_dt_struct))).value;
  const uint32_t struct_block_size =
      ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, size_dt_struct))).value;
  ZX_ASSERT(struct_block_offset < fdt.size());
  ZX_ASSERT(fdt.size() - struct_block_offset >= struct_block_size);

  const uint8_t* struct_block_base = fdt.data() + struct_block_offset;
  ByteView struct_block(struct_block_base, struct_block_size);
  ZX_ASSERT(struct_block_size > 0);
  ZX_ASSERT(ReadBigEndianUint32(struct_block.substr(struct_block_size - sizeof(uint32_t))).value ==
            FDT_END);

  const uint32_t string_block_offset =
      ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, off_dt_strings))).value;
  const uint32_t string_block_size =
      ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, size_dt_strings))).value;
  ZX_ASSERT(string_block_offset <= fdt.size());
  ZX_ASSERT(fdt.size() - struct_block_offset >= struct_block_size);

  const uint8_t* string_block_base = fdt.data() + string_block_offset;
  std::string_view string_block(reinterpret_cast<const char*>(string_block_base),
                                string_block_size);

  const uint32_t mem_rsvmap_offset =
      ReadBigEndianUint32(fdt.substr(offsetof(struct_fdt_header, off_mem_rsvmap))).value;
  ZX_ASSERT(mem_rsvmap_offset <= fdt.size());
  const uint8_t* mem_rsvmap_base = fdt.data() + mem_rsvmap_offset;
  ByteView mem_rsvmap{mem_rsvmap_base, fdt.size() - mem_rsvmap_offset};

  fdt_ = fdt;
  struct_block_ = struct_block;
  string_block_ = string_block;
  mem_rsvmap_ = mem_rsvmap;
}

ByteView Devicetree::EndOfPropertyBlock(ByteView prop) { return ReadPropertyBlock(prop).tail; }

void Devicetree::WalkTree(NodeVisitor pre_order_visitor, NodeVisitor post_order_visitor) {
  // |path| will point to stack-owned Nodes as we recurse.
  NodePath path;
  ByteView unprocessed = struct_block_;
  while (!unprocessed.empty()) {
    auto [token, tail] = ReadBigEndianUint32(unprocessed);
    unprocessed = tail;
    switch (token) {
      case FDT_NOP:
        break;
      case FDT_BEGIN_NODE:
        unprocessed = WalkSubtree(unprocessed, &path, pre_order_visitor, post_order_visitor, true);
        break;
      case FDT_END:
        return;
      default:
        ZX_PANIC("unknown devicetree token: %#x", token);
    }
  }
}

// Recursively walks a subtree and returns its unprocessed tail.
// It should be invariant that the subtree begins just after the
// FDT_BEGIN_NODE token.
ByteView Devicetree::WalkSubtree(ByteView subtree, NodePath* path, NodeVisitor& pre_order_visitor,
                                 NodeVisitor& post_order_visitor, bool visit) {
  ByteView unprocessed = subtree;

  // The node name follows the begin token.
  size_t name_end = unprocessed.find_first_of('\0');
  ZX_ASSERT_MSG(name_end != ByteView::npos, "unterminated node name");
  std::string_view name{reinterpret_cast<const char*>(unprocessed.data()), name_end};
  Node node{name};
  path->push_back(&node);
  unprocessed.remove_prefix(StructBlockAlign(name_end + 1));

  // Seek past all no-op tokens and properties.
  ByteView props_block = unprocessed;
  while (!unprocessed.empty()) {
    auto [token, tail] = ReadBigEndianUint32(unprocessed);
    switch (token) {
      case FDT_NOP:
        unprocessed = tail;
        continue;
      case FDT_PROP:
        unprocessed = EndOfPropertyBlock(tail);
        continue;
      case FDT_END_NODE:
        break;
    }
    break;
  }

  // Recall that it is a simplifying assumption of Properties that it must
  // be instantiated with a block that is either empty or that which begins
  // just after a property token.
  props_block.remove_suffix(unprocessed.size());

  auto call = [&](auto&& walker) { return walker(*path, Properties(props_block, string_block_)); };
  bool post_visit = visit;
  if (visit) {
    while (!props_block.empty()) {
      auto [token, tail] = ReadBigEndianUint32(props_block);
      props_block = tail;
      switch (token) {
        case FDT_PROP:
          break;
        case FDT_END_NODE:
        default:
          continue;
      }
      // Reached only at the end of the property block.
      break;
    }
    visit = call(pre_order_visitor);
  }

  // Walk all subtrees of |node|.
  while (!unprocessed.empty()) {
    auto [token, tail] = ReadBigEndianUint32(unprocessed);
    unprocessed = tail;
    switch (token) {
      case FDT_NOP:
        continue;
      case FDT_BEGIN_NODE:
        unprocessed = WalkSubtree(unprocessed, path, pre_order_visitor, post_order_visitor, visit);
        continue;
      case FDT_END_NODE:
        break;
    }
    break;
  }

  if (post_visit) {
    call(post_order_visitor);
  }

  path->pop_back();
  return unprocessed;
}

MemoryReservations::iterator MemoryReservations::begin() const {
  iterator it;
  it.mem_rsvmap_ = mem_rsvmap_;
  it.Normalize();
  return it;
}

using RawRsvMapEntry = std::array<uint64_t, 2>;

void MemoryReservations::iterator::Normalize() {
  constexpr RawRsvMapEntry kEnd{};
  if (mem_rsvmap_.size() < sizeof(RawRsvMapEntry) ||
      *reinterpret_cast<const RawRsvMapEntry*>(mem_rsvmap_.data()) == kEnd) {
    *this = {};
  }
}

MemoryReservations::iterator& MemoryReservations::iterator::operator++() {
  mem_rsvmap_.remove_prefix(sizeof(RawRsvMapEntry));
  Normalize();
  return *this;
}

MemoryReservations::value_type MemoryReservations::iterator::operator*() const {
  auto [start, tail] = ReadBigEndianUint64(mem_rsvmap_);
  auto [size, rest] = ReadBigEndianUint64(tail);
  return {start, size};
}

}  // namespace devicetree
