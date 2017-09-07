// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Field declarations for the trace record format.
//

#pragma once

#include <trace-engine/types.h>

#ifdef __cplusplus

#include <fbl/type_support.h>

namespace trace {

inline constexpr size_t Pad(size_t size) {
    return size + ((8 - (size & 7)) & 7);
}

inline constexpr size_t BytesToWords(size_t num_bytes) {
    return Pad(num_bytes) / sizeof(uint64_t);
}

inline constexpr size_t WordsToBytes(size_t num_words) {
    return num_words * sizeof(uint64_t);
}

// Casts an enum's value to its underlying type.
template <typename T>
inline constexpr typename fbl::underlying_type<T>::type ToUnderlyingType(T value) {
    return static_cast<typename fbl::underlying_type<T>::type>(value);
}

// Describes the layout of a bit-field packed into a 64-bit word.
template <size_t begin, size_t end>
struct Field {
    static_assert(begin < sizeof(uint64_t) * 8, "begin is out of bounds");
    static_assert(end < sizeof(uint64_t) * 8, "end is out of bounds");
    static_assert(begin <= end, "begin must not be larger than end");
    static_assert(end - begin + 1 < 64, "must be a part of a word, not a whole word");

    static constexpr uint64_t kMask = (uint64_t(1) << (end - begin + 1)) - 1;

    static constexpr uint64_t Make(uint64_t value) { return value << begin; }

    template <typename U>
    static constexpr U Get(uint64_t word) {
        return static_cast<U>((word >> begin) & kMask);
    }

    static constexpr void Set(uint64_t& word, uint64_t value) {
        word = (word & ~(kMask << begin)) | (value << begin);
    }
};

struct ArgumentFields {
    using Type = Field<0, 3>;
    using ArgumentSize = Field<4, 15>;
    using NameRef = Field<16, 31>;
};

struct Int32ArgumentFields : ArgumentFields {
    using Value = Field<32, 63>;
};

struct Uint32ArgumentFields : ArgumentFields {
    using Value = Field<32, 63>;
};

struct StringArgumentFields : ArgumentFields {
    using Index = Field<32, 47>;
};

struct RecordFields {
    static constexpr size_t kMaxRecordSizeWords = 0xfff;
    static constexpr size_t kMaxRecordSizeBytes = WordsToBytes(kMaxRecordSizeWords);

    using Type = Field<0, 3>;
    using RecordSize = Field<4, 15>;
};

struct MetadataRecordFields : RecordFields {
    using MetadataType = Field<16, 19>;
};

struct ProviderInfoMetadataRecordFields : MetadataRecordFields {
    static constexpr size_t kMaxNameLength = 0xff;

    using Id = Field<20, 51>;
    using NameLength = Field<52, 59>;
};

struct ProviderSectionMetadataRecordFields : MetadataRecordFields {
    using Id = Field<20, 51>;
};

using InitializationRecordFields = RecordFields;

struct StringRecordFields : RecordFields {
    using StringIndex = Field<16, 30>;
    using StringLength = Field<32, 46>;
};

struct ThreadRecordFields : RecordFields {
    using ThreadIndex = Field<16, 23>;
};

struct EventRecordFields : RecordFields {
    using EventType = Field<16, 19>;
    using ArgumentCount = Field<20, 23>;
    using ThreadRef = Field<24, 31>;
    using CategoryStringRef = Field<32, 47>;
    using NameStringRef = Field<48, 63>;
};

struct KernelObjectRecordFields : RecordFields {
    using ObjectType = Field<16, 23>;
    using NameStringRef = Field<24, 39>;
    using ArgumentCount = Field<40, 43>;
};

struct ContextSwitchRecordFields : RecordFields {
    using CpuNumber = Field<16, 23>;
    using OutgoingThreadState = Field<24, 27>;
    using OutgoingThreadRef = Field<28, 35>;
    using IncomingThreadRef = Field<36, 43>;
};

struct LogRecordFields : RecordFields {
    static constexpr size_t kMaxMessageLength = 0x7fff;
    using LogMessageLength = Field<16, 30>;
    using ThreadRef = Field<32, 39>;
};

} // namespace trace

#endif // __cplusplus
