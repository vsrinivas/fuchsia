// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>
#include <climits>
#include <elf.h>
#include <memory>

#include "memory.h"

namespace debugserver {
namespace elf {

// 32+64 support, bi-endian, mmap support can come later when needed

#if UINT_MAX == ULONG_MAX

using Header = Elf32_Ehdr;
using SegmentHeader = Elf32_Phdr;
using SectionHeader = Elf32_Shdr;
using Symbol = Elf32_Sym;

#else

using Header = Elf64_Ehdr;
using SegmentHeader = Elf64_Phdr;
using SectionHeader = Elf64_Shdr;
using Symbol = Elf64_Sym;

#endif

enum class Error {
  OK = 0,
  IO,
  BADELF,
  NOMEM,
};

const char* ErrorName(Error er);

class Reader;

class SectionContents {
 public:
  ~SectionContents();

  // Return the size in bytes of the section.
  // TODO(dje): 32x64
  size_t GetSize() const { return header_.sh_size; }

  // Return the number of entries in the section, assuming the section is
  // one that has "entries". E.g., symbol sections have entries, text sections
  // do not. For sections that don't have "entries" zero is returned.
  size_t GetNumEntries() const;

  // Fetch symbol |entry_number|.
  // The section must have type SHT_SYMTAB or SHT_DYNSYM.
  // WARNING: Space for the result may be reused for each call.
  // [We don't byteswap today, but when we do that is how this will work.
  // Symbols are generally used to create internal symbol tables and thus
  // are generally discarded immediately after use.]
  const Symbol& GetSymbolEntry(size_t entry_number);

  const SectionHeader& header() const { return header_; }

  const void* contents() const { return contents_; }

 private:
  friend class Reader;

  // Takes ownership of |contents|.
  // TODO(dje): separate method for mmap
  SectionContents(const SectionHeader& header, void* contents);

  // A copy is made of the header to separate the lifetime of the section's
  // contents from Reader. We could just save the pieces we use/need but
  // this is simple enough and saves us from having to continually add more.
  // Note that while we don't byteswap today, SectionHeader contains the
  // ready-to-use version.
  const SectionHeader header_;
  void* contents_;
};

class Reader {
 public:
  static Error Create(const util::Memory& reader,
                      uint32_t options, uintptr_t base,
                      std::unique_ptr<Reader>* out);
  ~Reader();

  // Read the ELF header at offset |base| in |m|.
  // The header is written in to |hdr|.
  static bool ReadHeader(const util::Memory& m, uintptr_t base, Header* hdr);

  // Return true if |hdr| is a valid ELF header.
  static bool VerifyHeader(const Header* hdr);

  const Header& header() { return header_; }

  // Return the number of program segments.
  size_t GetNumSegments() const { return header_.e_phnum; }

  // Read the program segment headers in.
  // This is a no-op if they are already read in.
  // This must be called before any call to GetSegment().
  Error ReadSegmentHeaders();

  // Free space allocated by ReadSegmentHeaders();
  void FreeSegmentHeaders();

  // Return the program segment header of |segment_number|.
  // |segment_number| must be valid, and ReadSegmentHeaders() must have
  // already been called.
  const SegmentHeader& GetSegmentHeader(size_t segment_number);

  // Return the number of sections.
  size_t GetNumSections() const { return header_.e_shnum; }

  // Read the section headers in.
  // This is a no-op if they are already read in.
  // This must be called before any call to GetSection().
  Error ReadSectionHeaders();

  // Free space allocated by ReadSectionHeaders();
  void FreeSectionHeaders();

  // Return the section header of |section_number|.
  // |section_number| must be valid, and ReadSectionHeaders() must have
  // already been called.
  const SectionHeader& GetSectionHeader(size_t section_number);

  // Fetch the contents of |sh|.
  // This version malloc's space for the section, reads the contents into
  // the buffer, and assigns it to SectionContents.
  Error GetSectionContents(const SectionHeader& sh,
                           std::unique_ptr<SectionContents>* out_contents);

  // Maximum length in bytes of a build id.
  static constexpr size_t kMaxBuildIdSize = 64;

  // Store the build id, if present, in |buf|.
  // |buf_size| must be at least kMaxBuildIdSize * 2 + 1.
  // If a build id is not found |buf| is "" and OK is returned.
  // TODO(dje): As with other changes deferred for later,
  // one might consider using std::string here. Later.
  Error ReadBuildId(char* buf, size_t buf_size);

  // Read |length| bytes at |address| in the ELF object an store in |buffer|.
  // |address| is the offset from the beginning of the ELF object.
  bool Read(uintptr_t address, void* buffer, size_t length) const;

 private:
  Reader(const util::Memory& reader, uintptr_t base);

  // This is the API to read/write from wherever the ELF object lives.
  // It could be in process memory, or in a file, or wherever.
  const util::Memory& reader_;

  // The offset in |reader_| of the start of the ELF object.
  const uintptr_t base_;

  Header header_;

  const SegmentHeader* segment_headers_ = nullptr;
  const SectionHeader* section_headers_ = nullptr;
};

}  // namespace elf
}  // namespace debugserver
