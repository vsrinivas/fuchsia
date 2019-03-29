// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_VMO_BLOCK_H_
#define LIB_INSPECT_VMO_BLOCK_H_

#include "limits.h"

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>

#include <zircon/types.h>

namespace inspect {
namespace vmo {

enum class BlockType {
    kFree = 0,
    kReserved = 1,
    kHeader = 2,
    kObjectValue = 3,
    kIntValue = 4,
    kUintValue = 5,
    kDoubleValue = 6,
    kPropertyValue = 7,
    kExtent = 8,
    kName = 9,
    kTombstone = 10
};

enum class PropertyFormat {
    kUtf8 = 0,
    kBinary = 1
};

namespace internal {

using BlockOrder = uint32_t;
using BlockIndex = uint64_t;

// Returns the smallest order such that (kMinOrderSize << order) >= size.
// Size must be non-zero.
constexpr BlockOrder FitOrder(size_t size) {
    auto ret = 64 - __builtin_clzl(size - 1) - kMinOrderShift;
    return static_cast<BlockOrder>(ret);
}

// Structure of the block header and payload.
struct Block {
    union {
        uint64_t header;
        char header_data[8];
    };
    union {
        int64_t i64;
        uint64_t u64;
        double f64;
        char data[8];
    } payload;
};

static_assert(sizeof(Block) == 16, "Block header must be 16 bytes");
static_assert(sizeof(Block) == kMinOrderSize,
              "Minimum allocation size must exactly hold a block header");

// Describes the layout of a bit-field packed into a 64-bit word.
template <size_t begin, size_t end>
struct Field {
    static_assert(begin < sizeof(uint64_t) * 8, "begin is out of bounds");
    static_assert(end < sizeof(uint64_t) * 8, "end is out of bounds");
    static_assert(begin <= end, "begin must not be larger than end");
    static_assert(end - begin + 1 < 64, "must be a part of a word, not a whole word");

    static constexpr uint64_t kMask = (uint64_t(1) << (end - begin + 1)) - 1;

    template <typename T>
    static constexpr uint64_t Make(T value) {
        return static_cast<uint64_t>(value) << begin;
    }

    template <typename U>
    static constexpr U Get(uint64_t word) {
        return static_cast<U>((word >> (begin % 64)) & kMask);
    }

    static constexpr void Set(uint64_t* word, uint64_t value) {
        *word = (*word & ~(kMask << begin)) | (value << begin);
    }
};

// Describes the base fields present for all blocks.
struct BlockFields {
    using Order = Field<0, 3>;
    using Type = Field<4, 7>;
};

struct HeaderBlockFields : public BlockFields {
    using Version = Field<8, 31>;
    using MagicNumber = Field<32, 63>;
};

struct FreeBlockFields : public BlockFields {
    using NextFreeBlock = Field<8, 35>;
};

// Describes the fields common to all value blocks.
struct ValueBlockFields : public BlockFields {
    using ParentIndex = Field<8, 35>;
    using NameIndex = Field<36, 63>;
};

struct PropertyBlockPayload {
    using TotalLength = Field<0, 31>;
    using ExtentIndex = Field<32, 59>;
    using Flags = Field<60, 63>;
};

struct ExtentBlockFields : public BlockFields {
    using NextExtentIndex = Field<8, 35>;
};

struct NameBlockFields : public BlockFields {
    using Length = Field<8, 19>;
};

constexpr BlockOrder GetOrder(const Block* block) {
    return BlockFields::Order::Get<BlockOrder>(block->header);
}

constexpr BlockType GetType(const Block* block) {
    return BlockFields::Type::Get<BlockType>(block->header);
}

constexpr size_t PayloadCapacity(BlockOrder order) {
    return OrderToSize(order) - sizeof(Block::header);
}

constexpr size_t BlockSizeForPayload(size_t payload_size) {
    return fbl::max(payload_size + sizeof(Block::header), kMinOrderSize);
}

constexpr size_t kMaxPayloadSize = kMaxOrderSize - sizeof(Block::header);

} // namespace internal
} // namespace vmo
} // namespace inspect

#endif // LIB_INSPECT_VMO_BLOCK_H_
