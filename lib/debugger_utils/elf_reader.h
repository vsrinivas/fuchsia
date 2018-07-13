// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#ifdef __APPLE__
// TODO(dje): Private copy until available on osx.
#include <garnet/lib/debugger_utils/third_party/musl/include/elf.h>
#else
#include <elf.h>
#endif
#include <memory>
#include <string>

#include "byte_block.h"

namespace debugserver {

// 32+64 support, bi-endian, mmap support can come later when needed

#if UINT_MAX == ULONG_MAX

using ElfHeader = Elf32_Ehdr;
using ElfSegmentHeader = Elf32_Phdr;
using ElfSectionHeader = Elf32_Shdr;
using ElfRawSymbol = Elf32_Sym;

#else

using ElfHeader = Elf64_Ehdr;
using ElfSegmentHeader = Elf64_Phdr;
using ElfSectionHeader = Elf64_Shdr;
using ElfRawSymbol = Elf64_Sym;

#endif

enum class ElfError {
  OK = 0,
  IO,
  BADELF,
  NOMEM,
};

const char* ElfErrorName(ElfError er);

class ElfReader;

class ElfSectionContents {
 public:
  ~ElfSectionContents();

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
  const ElfRawSymbol& GetSymbolEntry(size_t entry_number);

  const ElfSectionHeader& header() const { return header_; }

  const void* contents() const { return contents_; }

 private:
  friend class ElfReader;

  // Takes ownership of |contents|.
  // TODO(dje): separate method for mmap
  ElfSectionContents(const ElfSectionHeader& header, void* contents);

  // A copy is made of the header to separate the lifetime of the section's
  // contents from Reader. We could just save the pieces we use/need but
  // this is simple enough and saves us from having to continually add more.
  // Note that while we don't byteswap today, ElfSectionHeader contains the
  // ready-to-use version.
  const ElfSectionHeader header_;
  void* contents_;
};

class ElfReader {
 public:
  static ElfError Create(const std::string& file_name,
                         std::shared_ptr<ByteBlock> byte_block,
                         uint32_t options, uint64_t base,
                         std::unique_ptr<ElfReader>* out);
  ~ElfReader();

  const std::string& file_name() const { return file_name_; }

  // Read the ELF header at offset |base| in |m|.
  // The header is written in to |hdr|.
  static bool ReadHeader(const ByteBlock& m, uint64_t base, ElfHeader* hdr);

  // Return true if |hdr| is a valid ELF header.
  static bool VerifyHeader(const ElfHeader* hdr);

  const ElfHeader& header() { return header_; }

  // Return the number of program segments.
  size_t GetNumSegments() const { return header_.e_phnum; }

  // Read the program segment headers in.
  // This is a no-op if they are already read in.
  // This must be called before any call to GetSegment().
  ElfError ReadSegmentHeaders();

  // Free space allocated by ReadSegmentHeaders();
  void FreeSegmentHeaders();

  // Return the program segment header of |segment_number|.
  // |segment_number| must be valid, and ReadSegmentHeaders() must have
  // already been called.
  const ElfSegmentHeader& GetSegmentHeader(size_t segment_number);

  // Return the number of sections.
  size_t GetNumSections() const { return header_.e_shnum; }

  // Read the section headers in.
  // This is a no-op if they are already read in.
  // This must be called before any call to GetSection().
  ElfError ReadSectionHeaders();

  // Free space allocated by ReadSectionHeaders();
  void FreeSectionHeaders();

  // Return the section header of |section_number|.
  // |section_number| must be valid, and ReadSectionHeaders() must have
  // already been called.
  const ElfSectionHeader& GetSectionHeader(size_t section_number);

  // Return the section header with type |type|, an SHT_* value.
  // Returns nullptr if not found.
  const ElfSectionHeader* GetSectionHeaderByType(unsigned type);

  // Fetch the contents of |sh|.
  // This version malloc's space for the section, reads the contents into
  // the buffer, and assigns it to SectionContents.
  ElfError GetSectionContents(
      const ElfSectionHeader& sh,
      std::unique_ptr<ElfSectionContents>* out_contents);

  // Maximum length in bytes of a build id.
  static constexpr size_t kMaxBuildIdSize = 64;

  // Store the build id, if present, in |buf|.
  // |buf_size| must be at least kMaxBuildIdSize * 2 + 1.
  // If a build id is not found |buf| is "" and OK is returned.
  // TODO(dje): As with other changes deferred for later,
  // one might consider using std::string here. Later.
  ElfError ReadBuildId(char* buf, size_t buf_size);

  // Read |length| bytes at |address| in the ELF object an store in |buffer|.
  // |address| is the offset from the beginning of the ELF object.
  bool Read(uint64_t address, void* buffer, size_t length) const;

 private:
  ElfReader(const std::string& file_name, std::shared_ptr<ByteBlock> byte_block,
            uint64_t base);

  // For debugging/informational purposes only.
  const std::string file_name_;

  // This is the API to read/write from wherever the ELF object lives.
  // It could be in process memory, or in a file, or wherever.
  const std::shared_ptr<ByteBlock> byte_block_;

  // The offset in |byte_block_| of the start of the ELF object.
  const uint64_t base_;

  ElfHeader header_;

  const ElfSegmentHeader* segment_headers_ = nullptr;
  const ElfSectionHeader* section_headers_ = nullptr;
};

}  // namespace debugserver
