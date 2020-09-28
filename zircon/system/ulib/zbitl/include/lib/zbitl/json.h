// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_JSON_H_
#define LIB_ZBITL_JSON_H_

#include <zircon/boot/image.h>

#include <optional>

#include "item.h"

// This provides APIs for representing ZBI items and whole ZBIs in JSON.  These
// APIs are compatible with rapidjson, but this code does not depend directly
// on rapidjson.  Instead, it provides template functions that accept any
// object compatible with rapidjson's SAX API.

// TODO(fxbug.dev/49438): document schema; parse/validate JSON -> header / payload(?)

namespace zbitl {

/// Takes any class compatible with the rapidjson::Writer API.  This emits keys
/// and values describing the header fields.  It should be called after
/// writer.StartObject() and before writer.EndObject().  It doesn't call those
/// itself in case the caller wants to add the "contents" key (or others).
template <typename Writer>
void JsonWriteHeader(Writer&& writer, const zbi_header_t& header,
                     std::optional<uint32_t> offset = {}) {
  if (offset.has_value()) {
    writer.Key("offset");
    writer.Uint(*offset);
  }

  writer.Key("type");
  if (auto name = TypeName(header); name.empty()) {
    writer.Uint(header.type);
  } else {
    writer.String(name.data(), static_cast<uint32_t>(name.size()));
  }

  writer.Key("size");
  writer.Uint(header.length);

  // Storage types have uncompressed_size.  Otherwise write generic "extra",
  // but elide it when zero.
  if (TypeIsStorage(header) && (header.flags & ZBI_FLAG_STORAGE_COMPRESSED)) {
    writer.Key("uncompressed_size");
    writer.Uint(header.extra);
  } else {
    uint32_t expected_extra = header.type == ZBI_TYPE_CONTAINER ? ZBI_CONTAINER_MAGIC
                              : TypeIsStorage(header)           ? header.length
                                                                : 0;
    if (header.extra != expected_extra) {
      writer.Key("extra");
      writer.Uint(header.extra);
    }
  }

  // Write exact flags if it has anything unusual.
  uint32_t known_flags = ZBI_FLAG_CRC32 | ZBI_FLAG_VERSION;
  if (TypeIsStorage(header)) {
    known_flags |= ZBI_FLAG_STORAGE_COMPRESSED;
  }
  if (!(header.flags & ZBI_FLAG_VERSION) || (header.flags & ~known_flags)) {
    writer.Key("flags");
    writer.Uint(header.flags);
  }

  if (header.reserved0 != 0) {
    writer.Key("reserved0");
    writer.Uint(header.reserved0);
  }
  if (header.reserved1 != 0) {
    writer.Key("reserved1");
    writer.Uint(header.reserved1);
  }

  // The "crc32" field isn't mentioned when it's disabled, even if it doesn't
  // have the canonical ZBI_ITEM_NO_CRC32 value.
  if (header.flags & ZBI_FLAG_CRC32) {
    writer.Key("crc32");
    writer.Uint(header.crc32);
  }
}

/// Takes any class compatible with the rapidjson::Writer API and emits a JSON
/// object describing the item's header details.  This omits "contents" fields.
template <typename Writer>
void JsonWriteItem(Writer&& writer, const zbi_header_t& header,
                   std::optional<uint32_t> offset = {}) {
  writer.StartObject();
  JsonWriteHeader(writer, header, offset);
  writer.EndObject();
}

/// Takes any class compatible with the rapidjson::Writer API and emits a JSON
/// object describing the item's header details.  If there is a nonempty
/// payload, this calls `contents(writer, key, header, payload)` and that
/// should call `writer.Key(key)` and some appropriate value type if it wants
/// to describe the contents; if it does nothing, the output is the same as for
/// JsonWriteItem (above).
template <typename Writer, typename Payload, typename Contents>
void JsonWriteItemWithContents(Writer&& writer, Contents&& contents, const zbi_header_t& header,
                               Payload&& payload, std::optional<uint32_t> offset = {}) {
  writer.StartObject();
  JsonWriteHeader(writer, header, offset);
  if (header.length > 0) {
    contents(writer, "contents", header, payload);
  }
  writer.EndObject();
}

struct JsonIgnoreContents {
  template <typename... Args>
  void operator()(Args&&...) {}
};

template <typename Writer, typename Zbi, typename Contents = JsonIgnoreContents>
void JsonWriteZbi(Writer&& writer, Zbi&& zbi, std::optional<uint32_t> offset = {},
                  Contents&& contents = {}) {
  // Advance the offset past the header and payload.
  auto advance_offset = [&offset](uint32_t payload_length) {
    if (offset) {
      *offset += static_cast<uint32_t>(sizeof(zbi_header_t));
      *offset += ZBI_ALIGN(payload_length);
    }
  };

  if (auto container = zbi.container_header(); container.is_ok()) {
    writer.StartObject();

    JsonWriteHeader(writer, container.value(), offset);
    advance_offset(0);

    writer.Key("items");

    writer.StartArray();
    for (auto [header, payload] : zbi) {
      JsonWriteItemWithContents(writer, contents, *header, payload, offset);
      advance_offset(header->length);
    }
    writer.EndArray();

    writer.EndObject();
  }
}

}  // namespace zbitl

#endif  // LIB_ZBITL_JSON_H_
