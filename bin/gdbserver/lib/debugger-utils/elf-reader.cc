// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "elf-reader.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

#include "util.h"

namespace debugserver {
namespace elf {

const char* ErrorName(Error err) {
  switch (err) {
  case Error::OK: return "OK";
  case Error::IO: return "IO";
  case Error::BADELF: return "BADELF";
  case Error::NOMEM: return "NOMEM";
  default: return "UNKNOWN";
  }
}

Error Reader::Create(const std::string& file_name,
                     std::shared_ptr<util::ByteBlock> byte_block,
                     uint32_t options, uint64_t base,
                     std::unique_ptr<Reader>* out) {
  FXL_DCHECK(options == 0);
  Reader* er = new Reader(file_name, byte_block, base);
  if (!ReadHeader(*byte_block, base, &er->header_)) {
    delete er;
    return Error::IO;
  }
  if (!VerifyHeader(&er->header_)) {
    delete er;
    return Error::BADELF;
  }
  *out = std::unique_ptr<Reader>(er);
  return Error::OK;
}

Reader::Reader(const std::string& file_name,
               std::shared_ptr<util::ByteBlock> byte_block,
               uint64_t base)
  : file_name_(file_name),
    byte_block_(byte_block),
    base_(base) {
}

Reader::~Reader() {
  FreeSegmentHeaders();
  FreeSectionHeaders();
}

// static
bool Reader::ReadHeader(const util::ByteBlock& m, uint64_t base, Header* hdr) {
  return m.Read(base, hdr, sizeof(*hdr));
}

// static
bool Reader::VerifyHeader(const Header* hdr) {
  if (memcmp(hdr->e_ident, ELFMAG, SELFMAG))
    return false;
  // TODO(dje): Support larger entries.
  if (hdr->e_ehsize != sizeof(Header))
    return false;
  if (hdr->e_phentsize != sizeof(SegmentHeader))
    return false;
  if (hdr->e_shentsize != sizeof(SectionHeader))
    return false;
  // TODO(dje): Could add more checks.
  return true;
}

Error Reader::ReadSegmentHeaders() {
  if (segment_headers_)
    return Error::OK;
  size_t num_segments = GetNumSegments();
  auto seg_hdrs = new SegmentHeader[num_segments];
  if (!byte_block_->Read(base_ + header_.e_phoff, seg_hdrs,
                         num_segments * sizeof(SegmentHeader))) {
    delete[] seg_hdrs;
    return Error::IO;
  }
  segment_headers_ = seg_hdrs;
  return Error::OK;
}

void Reader::FreeSegmentHeaders() {
  if (segment_headers_)
    delete[] segment_headers_;
  segment_headers_ = nullptr;
}

const SegmentHeader& Reader::GetSegmentHeader(size_t segment_number) {
  FXL_DCHECK(segment_headers_);
  FXL_DCHECK(segment_number < GetNumSegments());
  return segment_headers_[segment_number];
}

Error Reader::ReadSectionHeaders() {
  if (section_headers_)
    return Error::OK;
  size_t num_sections = GetNumSections();
  auto scn_hdrs = new SectionHeader[num_sections];
  if (!byte_block_->Read(base_ + header_.e_shoff, scn_hdrs,
                         num_sections * sizeof(SectionHeader))) {
    delete[] scn_hdrs;
    return Error::IO;
  }
  section_headers_ = scn_hdrs;
  return Error::OK;
}

void Reader::FreeSectionHeaders() {
  if (section_headers_)
    delete[] section_headers_;
  section_headers_ = nullptr;
}

const SectionHeader& Reader::GetSectionHeader(size_t section_number) {
  FXL_DCHECK(section_headers_);
  FXL_DCHECK(section_number < GetNumSections());
  return section_headers_[section_number];
}

const SectionHeader* Reader::GetSectionHeaderByType(unsigned type) {
  size_t num_sections = GetNumSections();
  for (size_t i = 0; i < num_sections; ++i) {
    const SectionHeader& shdr = GetSectionHeader(i);
    if (shdr.sh_type == type)
      return &shdr;
  }
  return nullptr;
}

Error Reader::GetSectionContents(
    const SectionHeader& sh,
    std::unique_ptr<SectionContents>* out_contents) {
  void* buffer = malloc(sh.sh_size);
  if (!buffer) {
    FXL_LOG(ERROR) << "OOM getting space for section contents";
    return Error::NOMEM;
  }

  if (!byte_block_->Read(base_ + sh.sh_offset, buffer, sh.sh_size)) {
    FXL_LOG(ERROR) << "Error reading section contents";
    return Error::IO;
  }

  // TODO(dje): Handle malloc failures for new.
  auto contents = new SectionContents(sh, buffer);
  *out_contents = std::unique_ptr<SectionContents>(contents);
  return Error::OK;
}

Error Reader::ReadBuildId(char* buf, size_t buf_size) {
  uint64_t vaddr = base_;

  FXL_DCHECK(buf_size >= kMaxBuildIdSize * 2 + 1);

  Error rc = ReadSegmentHeaders();
  if (rc != Error::OK)
    return rc;

  size_t num_segments = GetNumSegments();

  for (size_t i = 0; i < num_segments; ++i) {
    const auto& phdr = GetSegmentHeader(i);
    if (phdr.p_type != PT_NOTE)
      continue;

    struct {
      Elf32_Nhdr hdr;
      char name[sizeof("GNU")];
    } note;
    uint64_t size = phdr.p_filesz;
    uint64_t offset = phdr.p_offset;
    while (size > sizeof(note)) {
      if (!byte_block_->Read(vaddr + offset, &note, sizeof(note)))
        return Error::IO;
      size_t header_size = sizeof(Elf32_Nhdr) + ((note.hdr.n_namesz + 3) & -4);
      size_t payload_size = (note.hdr.n_descsz + 3) & -4;
      offset += header_size;
      size -= header_size;
      uint64_t payload_vaddr = vaddr + offset;
      offset += payload_size;
      size -= payload_size;
      if (note.hdr.n_type != NT_GNU_BUILD_ID ||
          note.hdr.n_namesz != sizeof("GNU") ||
          memcmp(note.name, "GNU", sizeof("GNU")) != 0) {
        continue;
      }
      if (note.hdr.n_descsz > kMaxBuildIdSize) {
        // TODO(dje): Revisit.
        snprintf(buf, buf_size, "build_id_too_large_%u", note.hdr.n_descsz);
      } else {
        uint8_t buildid[kMaxBuildIdSize];
        if (!byte_block_->Read(payload_vaddr, buildid, note.hdr.n_descsz))
          return Error::IO;
        for (uint32_t i = 0; i < note.hdr.n_descsz; ++i) {
          snprintf(&buf[i * 2], 3, "%02x", buildid[i]);
        }
      }
      return Error::OK;
    }
  }

  *buf = '\0';
  return Error::OK;
}

SectionContents::SectionContents(const SectionHeader& header, void* contents)
  : header_(header), contents_(contents) {
  FXL_DCHECK(contents);
}

SectionContents::~SectionContents() {
  free(contents_);
}

size_t SectionContents::GetNumEntries() const {
  switch (header_.sh_type) {
  case SHT_SYMTAB:
  case SHT_DYNSYM:
    break;
  default:
    return 0;
  }

  FXL_DCHECK(header_.sh_entsize != 0);
  return header_.sh_size / header_.sh_entsize;
}

const RawSymbol& SectionContents::GetSymbolEntry(size_t entry_number) {
  FXL_DCHECK(header_.sh_type == SHT_SYMTAB || header_.sh_type == SHT_DYNSYM);
  FXL_DCHECK(entry_number < GetNumEntries());
  auto buf = reinterpret_cast<const char*>(contents_);
  auto sym = buf + entry_number * header_.sh_entsize;
  return *reinterpret_cast<const RawSymbol*>(sym);
}

}  // namespace elf
}  // namespace debugserver
