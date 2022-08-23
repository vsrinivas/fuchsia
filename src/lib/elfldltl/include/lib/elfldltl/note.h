// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_NOTE_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_NOTE_H_

#include <zircon/assert.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "layout.h"
#include "phdr.h"

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

  // For some reason `= default;` here doesn't permit constexpr.
  constexpr ElfNote& operator=(const ElfNote& other) {
    name = other.name;
    desc = other.desc;
    type = other.type;
    return *this;
  }

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

// elfldltl::PhdrFileNoteObserver(file_api_object, callback...) can be passed
// to elfldltl::DecodePhdrs to call each callback as if it were bool(ElfNote)
// on each note in the file, returning false the first time a callback returns
// false, or earlier if there is a problem reading notes from the file.  This
// will read both allocated and non-allocated notes, but always read them from
// the file rather than from memory (for allocated notes, it should be the same
// data as if the file were loaded into memory and then the notes read out of
// memory, unless the note contents are writable or RELRO data).
template <ElfData Data, class File, class Allocator, typename... Callback>
class PhdrFileNoteObserver
    : public PhdrObserver<PhdrFileNoteObserver<Data, File, Allocator, Callback...>,
                          ElfPhdrType::kNote> {
 public:
  static_assert((std::is_invocable_r_v<bool, Callback, ElfNote> && ...));

  PhdrFileNoteObserver() = delete;

  // Copyable and/or movable if Allocator and Callback are.
  constexpr PhdrFileNoteObserver(const PhdrFileNoteObserver&) = default;
  constexpr PhdrFileNoteObserver(PhdrFileNoteObserver&&) noexcept = default;

  template <class Elf>
  explicit constexpr PhdrFileNoteObserver(Elf&& elf, File& file, Allocator allocator,
                                          Callback... callback)
      : file_(&file),
        allocator_(std::move(allocator)),
        callback_{std::forward<Callback>(callback)...} {
    static_assert(std::decay_t<Elf>::kData == Data);
    static_assert(std::is_copy_constructible_v<PhdrFileNoteObserver> ==
                  (std::is_copy_constructible_v<Allocator> &&
                   (std::is_copy_constructible_v<Callback> && ...)));
    static_assert(std::is_move_constructible_v<PhdrFileNoteObserver> ==
                  (std::is_move_constructible_v<Allocator> &&
                   (std::is_move_constructible_v<Callback> && ...)));
    static_assert(
        std::is_copy_assignable_v<PhdrFileNoteObserver> ==
        (std::is_copy_assignable_v<Allocator> && (std::is_copy_assignable_v<Callback> && ...)));
    static_assert(
        std::is_move_assignable_v<PhdrFileNoteObserver> ==
        (std::is_move_assignable_v<Allocator> && (std::is_move_assignable_v<Callback> && ...)));
  }

  // Copy-assignable and/or move-assignable if Allocator and Callback are.
  constexpr PhdrFileNoteObserver& operator=(const PhdrFileNoteObserver&) = default;
  constexpr PhdrFileNoteObserver& operator=(PhdrFileNoteObserver&&) noexcept = default;

  template <class Diag, typename Phdr>
  constexpr bool Observe(Diag& diag, PhdrTypeMatch<ElfPhdrType::kNote> type, const Phdr& phdr) {
    if (phdr.filesz == 0) [[unlikely]] {
      return true;
    }
    auto bytes = file_->template ReadArrayFromFile<std::byte>(phdr.offset, allocator_, phdr.filesz);
    if (!bytes) [[unlikely]] {
      return diag.FormatError("failed to read note segment from file");
    }
    for (const ElfNote& note : ElfNoteSegment<Data>{{bytes->data(), bytes->size()}}) {
      auto all_callbacks_ok = [&note](auto&&... callback) -> bool {
        return (callback(note) && ...);
      };
      if (!std::apply(all_callbacks_ok, callback_)) {
        return false;
      }
    }
    return true;
  }

  template <class Diag>
  bool Finish(Diag& diag) {
    return true;
  }

 private:
  File* file_;
  Allocator allocator_;
  std::tuple<Callback...> callback_;
};

// Deduction guide.  When used without template parameters, the first
// constructor argument is an empty Elf<...> object to identify the format; the
// second is always an lvalue reference (see memory.h); the third is forwarded
// as the allocator object used by File::ReadArrayFromFile<std::byte>; while
// the later arguments (some invocable objects like `bool(ElfNote)`) are moved
// or copied so they can safely be temporaries.  Use std::ref or std::cref to
// make the PhdrFileNoteObserver object hold a callback by reference instead.
template <class Elf, class File, typename Allocator, typename... Callback>
PhdrFileNoteObserver(Elf&&, File&, Allocator&&, Callback&&...)
    -> PhdrFileNoteObserver<std::decay_t<Elf>::kData, File, std::decay_t<Allocator>,
                            std::decay_t<Callback>...>;

// This returns a bool(ElfNote) callback object that can be passed to
// elfldltl::PhdrFileNoteObserver.  That callback updates build_id to the
// file's (first) build ID note.  If the optional second argument is true,
// that callback returns true even after it's found the build ID, so that
// PhdrFileNoteObserver would continue to call additional callbacks on this
// and other notes rather than finish immediately.
constexpr auto ObserveBuildIdNote(std::optional<ElfNote>& build_id, bool keep_going = false) {
  return [keep_going, &build_id](const ElfNote& note) -> bool {
    if (!build_id) {
      if (!note.IsBuildId()) {
        return true;
      }
      build_id = note;
    }
    return keep_going;
  };
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_NOTE_H_
