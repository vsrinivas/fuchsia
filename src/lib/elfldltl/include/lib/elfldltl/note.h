// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_NOTE_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_NOTE_H_

#include <zircon/assert.h>

#include <cstdio>
#include <string_view>

#include "layout.h"

namespace elfldltl {

// The usual way to use the note parser is via elfldltl::Elf<...>::NoteSegment,
// which is an alias for the NoteSegment class defined below.

// This represents one decoded ELF note.  It's created ephemerally to yield
// views on the name and desc (payload), along with the type value.
struct ElfNote {
  using Bytes = std::basic_string_view<std::byte>;

  ElfNote() = delete;

  constexpr ElfNote(const ElfNote&) = default;

  template <typename Header>
  ElfNote(const Header& nhdr, Bytes note)
      : name(std::string_view{reinterpret_cast<const char*>(note.data()), note.size()}.substr(
            nhdr.name_offset(), nhdr.namesz)),
        desc(note.substr(nhdr.desc_offset(), nhdr.descsz)),
        type(nhdr.type) {}

  // Match against an expected name.
  template <size_t N>
  constexpr bool Is(const char (&that_name)[N]) const {
    return name == std::string_view{that_name, N};
  }

  // Match against an expected name and type.
  template <typename T, size_t N>
  constexpr bool Is(const char (&that_name)[N], const T& that_type) const {
    static_assert(sizeof(T) <= sizeof(uint32_t));
    return type == static_cast<uint32_t>(that_type) && Is(that_name);
  }

  // Match a GNU build ID note.
  constexpr bool IsBuildId() const { return Is("GNU", ElfNoteType::kGnuBuildId); }

  // Call `out(char)` to emit each desc byte in hex.
  template <typename Out>
  constexpr void HexDump(Out&& out) const {
    constexpr auto& kDigits = "0123456789abcdef";
    for (auto byte : desc) {
      auto x = static_cast<uint8_t>(byte);
      out(kDigits[x >> 4]);
      out(kDigits[x & 0xf]);
    }
  }

  // Send the hex string of desc to the stdio stream.
  void HexDump(FILE* f) const {
    HexDump([f](char c) { putc(c, f); });
  }

  // Return the number of characters HexDump() will write.
  constexpr size_t HexSize() const { return desc.size() * 2; }

  // Fill a fixed-sized buffer with as many hex characters as will fit.
  template <size_t N>
  constexpr std::string_view HexString(char (&buffer)[N]) const {
    size_t i = 0;
    HexDump([&](char c) {
      if (i < N) {
        buffer[i++] = c;
      }
    });
    return {buffer, i};
  }

  std::string_view name;
  Bytes desc;
  uint32_t type;
};

// This is a forward-iterable container view of notes in a note segment,
// constructible from the raw bytes known to be properly aligned.
template <ElfData Data = ElfData::kNative>
class ElfNoteSegment {
 public:
  using Bytes = ElfNote::Bytes;

  using Nhdr = typename LayoutBase<Data>::Nhdr;

  class iterator {
   public:
    constexpr iterator() = default;
    constexpr iterator(const iterator&) = default;

    constexpr bool operator==(const iterator& other) const {
      return other.notes_.data() == notes_.data() && other.notes_.size() == notes_.size();
    }

    constexpr bool operator!=(const iterator& other) const { return !(*this == other); }

    ElfNote operator*() const {
      ZX_DEBUG_ASSERT(Check(notes_));
      return {Header(notes_), notes_};
    }

    iterator& operator++() {  // prefix
      ZX_DEBUG_ASSERT(Check(notes_));
      notes_.remove_prefix(Header(notes_).size_bytes());
      if (!Check(notes_)) {
        // Ignore any odd bytes at the end of the segment and move to end()
        // state if there isn't space for another note.
        notes_.remove_prefix(notes_.size());
      }
      ZX_DEBUG_ASSERT(notes_.empty() || Check(notes_));
      return *this;
    }

    iterator operator++(int) {  // postfix
      iterator result = *this;
      ++*this;
      return result;
    }

   private:
    friend ElfNoteSegment;

    // notes_ holds the remainder to be iterated over.  It's always empty (the
    // end() state), or else contains at least one whole valid note.

    explicit constexpr iterator(Bytes notes) : notes_(notes) {
      ZX_DEBUG_ASSERT(notes_.empty() || Check(notes_));
    }

    Bytes notes_;
  };

  using const_iterator = iterator;

  ElfNoteSegment() = delete;

  constexpr ElfNoteSegment(const ElfNoteSegment&) = default;

  explicit constexpr ElfNoteSegment(Bytes notes)
      : notes_(notes.size() < sizeof(Nhdr) ? Bytes{} : notes) {}

  iterator begin() const { return Check(notes_) ? iterator{notes_} : end(); }

  iterator end() const { return iterator{notes_.substr(notes_.size())}; }

 private:
  // This is safe only if the size has been checked.
  static auto& Header(Bytes data) { return *reinterpret_cast<const Nhdr*>(data.data()); }

  // Returns true if the data starts with a valid note.
  static bool Check(Bytes data) {
    return data.size() >= sizeof(Nhdr) && Check(Header(data), data.size());
  }

  // Returns true if the header is valid for the given size.
  static constexpr bool Check(const Nhdr& hdr, size_t size) {
    // Take pains to avoid integer overflow.
    const size_t name_pad = Nhdr::Align(hdr.namesz) - hdr.namesz;
    const size_t desc_pad = Nhdr::Align(hdr.descsz) - hdr.descsz;
    return size - hdr.name_offset() >= hdr.namesz &&
           size - hdr.name_offset() - hdr.namesz >= name_pad &&
           size - hdr.desc_offset() >= hdr.descsz &&
           size - hdr.desc_offset() - hdr.descsz >= desc_pad;
  }

  Bytes notes_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_NOTE_H_
