// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/note.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#include <zxtest/zxtest.h>

namespace {

#if defined(__GLIBC__) || defined(__Fuchsia__)
#define HAVE_OPEN_MEMSTREAM 1
#else
#define HAVE_OPEN_MEMSTREAM 0
#endif

class StringFile {
 public:
  StringFile(const StringFile&) = delete;

  StringFile()
#if HAVE_OPEN_MEMSTREAM
      : file_(open_memstream(&buffer_, &buffer_size_))
#else
      : file_(tmpfile())
#endif
  {
    ASSERT_NOT_NULL(file_);
  }

  FILE* file() { return file_; }

  std::string contents() && {
#if HAVE_OPEN_MEMSTREAM
    EXPECT_EQ(0, fflush(file_));
    return std::string(buffer_, buffer_size_);
#endif
    EXPECT_EQ(0, fseek(file_, 0, SEEK_END), "fseek: %s", strerror(errno));
    std::string result(ftell(file_), 'x');
    rewind(file_);
    EXPECT_FALSE(ferror(file_));
    EXPECT_EQ(1, fread(result.data(), result.size(), 1, file_), "fread of %zu: %s", result.size(),
              strerror(errno));
    return result;
  }

  ~StringFile() {
    if (file_) {
      fclose(file_);
    }
#if defined(__GLIBC__) || defined(__Fuchsia__)
    free(buffer_);
#endif
  }

 private:
  FILE* file_ = nullptr;
#if HAVE_OPEN_MEMSTREAM
  char* buffer_ = nullptr;
  size_t buffer_size_ = 0;
#endif
};

template <auto Class, auto Data>
constexpr bool kAliasCheck = std::conjunction_v<
    std::is_same<typename elfldltl::Elf<Class, Data>::Note, elfldltl::ElfNote>,
    std::is_same<typename elfldltl::Elf<Class, Data>::NoteSegment, elfldltl::ElfNoteSegment<Data>>>;

static_assert(kAliasCheck<elfldltl::ElfClass::k32, elfldltl::ElfData::k2Msb>);
static_assert(kAliasCheck<elfldltl::ElfClass::k64, elfldltl::ElfData::k2Msb>);
static_assert(kAliasCheck<elfldltl::ElfClass::k32, elfldltl::ElfData::k2Lsb>);
static_assert(kAliasCheck<elfldltl::ElfClass::k64, elfldltl::ElfData::k2Lsb>);

TEST(ElfldltlNoteTests, Empty) {
  constexpr elfldltl::ElfNote::Bytes kData{};
  elfldltl::ElfNoteSegment notes(kData);

  for (auto note : notes) {
    // This should never be reached, but these statements ensure that the
    // intended API usages compile correctly.
    EXPECT_TRUE(note.name.empty());
    EXPECT_TRUE(note.desc.empty());
    EXPECT_EQ(note.type, uint32_t{0});
    FAIL("container should be empty");
  }
}

TEST(ElfldltlNoteTests, BuildId) {
  static constexpr std::byte kData[] = {
      std::byte{0},   std::byte{0},   std::byte{0},   std::byte{4},  // namesz
      std::byte{0},   std::byte{0},   std::byte{0},   std::byte{8},  // descsz
      std::byte{0},   std::byte{0},   std::byte{0},   std::byte{3},  // type
      std::byte{'G'}, std::byte{'N'}, std::byte{'U'}, std::byte{0},  // name[]
      std::byte{1},   std::byte{2},   std::byte{3},   std::byte{4},  // desc[]
      std::byte{5},   std::byte{6},   std::byte{7},   std::byte{8},
  };
  constexpr elfldltl::ElfNoteSegment<elfldltl::ElfData::k2Msb> notes({kData, sizeof(kData)});

  size_t count = 0;
  for (auto note : notes) {
    ++count;

    EXPECT_TRUE(note.IsBuildId());

    EXPECT_EQ(16, note.HexSize());

    std::string str;
    note.HexDump([&str](char c) { str += c; });
    EXPECT_STR_EQ(str, "0102030405060708");

    StringFile sf;
    note.HexDump(sf.file());
    EXPECT_STR_EQ(std::move(sf).contents(), "0102030405060708");
  }
  EXPECT_EQ(count, size_t{1});
}

}  // namespace
