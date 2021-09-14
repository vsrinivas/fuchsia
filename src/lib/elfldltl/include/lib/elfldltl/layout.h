// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LAYOUT_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LAYOUT_H_

#include <limits>

#include "constants.h"
#include "field.h"

namespace elfldltl {

// These types are parameterized by class (32-bit vs 64-bit) and data (byte
// order).  The traditional ELF names Byte, Half, Word, Xword, and Addr are
// used for accessor types that respect the byte order and class.  Note that
// redundant traditional names such as Offset are not used; Addr is used for
// all "address-sized" fields, whether they are offsets, addresses, or sizes.
//
// The Elf<Class, Data> template, abbreviated Elf32<Data> or Elf64<Data>, is
// the intended way to refer to all these types.  The Layout* base types are
// just doing some implementation sharing between template instantiations.
//
// When working with actual values rather than encoded ELF metadata formats,
// the standard uintNN_t types should be used.  The Elf::size_type type is an
// alias for the address-sized unsigned integer type, i.e. the host-side native
// type corresponding to Elf::Addr (which might be a byte-swapping type).
//
// The type and field names for struct types use the traditional terse ELF
// names, but without the traditional prefixes or capitalization.  Each field
// is a byte-order-respecting accessor for the natural underlying type or an
// enum class with the natural underlying type, and has a simple lowercase name
// with no prefix or suffix.  For compound fields, specific accessors are also
// provided to do the bit-field extraction.

// The basic types and some structure layouts are identical across bit width
// (ElfClass).  This base class handles differences in byte order (ElfData).
template <ElfData Data>
struct LayoutBase {
  static constexpr bool kSwap = Data != ElfData::kNative;

  template <typename T>
  struct UnsignedType {
    using type = UnsignedField<T, kSwap>;
    static_assert(sizeof(type) == sizeof(T));
    static_assert(alignof(type) == alignof(T));
  };

  template <typename T>
  using Unsigned = typename UnsignedType<T>::type;

  using Byte = Unsigned<uint8_t>;
  using Half = Unsigned<uint16_t>;
  using Word = Unsigned<uint32_t>;
  using Xword = Unsigned<uint64_t>;

  struct Nhdr {
    static constexpr uint32_t Align(uint32_t size) {
      return (size + kAlign - 1) & -uint32_t{kAlign};
    }

    constexpr uint32_t name_offset() const { return sizeof(*this); }

    constexpr uint32_t desc_offset() const { return name_offset() + Align(namesz); }

    constexpr uint32_t size_bytes() const { return desc_offset() + Align(descsz); }

    static constexpr uint32_t kAlign = 4;

    Word namesz;
    Word descsz;
    Word type;
  };
  static_assert(sizeof(Nhdr) == 12);
};

// Some header layouts vary by ElfClass, i.e. address size used in ELF
// metadata.  This is partially specialized by class below.
template <ElfClass Class, ElfData Data>
struct Layout;

// Layout details specific to 32-bit ELF.
template <ElfData Data>
struct Layout<ElfClass::k32, Data> : public LayoutBase<Data> {
  using LayoutBase<Data>::kSwap;
  using typename LayoutBase<Data>::Byte;
  using typename LayoutBase<Data>::Half;
  using typename LayoutBase<Data>::Word;

  using Addr = typename LayoutBase<Data>::template Unsigned<uint32_t>;

  struct Phdr {
    EnumField<ElfPhdrType, kSwap> type;
    Addr offset;
    Addr vaddr;
    Addr paddr;
    Addr filesz;
    Addr memsz;
    Word flags;
    Addr align;
  };

  struct Sym {
    Word name;
    Addr value;
    Addr size;
    Byte info;
    Byte other;
    Half shndx;
  };

  static constexpr unsigned int kRelTypeBits = 8;
};

// Layout details specific to 64-bit ELF.
template <ElfData Data>
struct Layout<ElfClass::k64, Data> : public LayoutBase<Data> {
  using LayoutBase<Data>::kSwap;
  using typename LayoutBase<Data>::Byte;
  using typename LayoutBase<Data>::Half;
  using typename LayoutBase<Data>::Word;

  using Addr = typename LayoutBase<Data>::template Unsigned<uint64_t>;

  struct Phdr {
    EnumField<ElfPhdrType, kSwap> type;
    Word flags;
    Addr offset;
    Addr vaddr;
    Addr paddr;
    Addr filesz;
    Addr memsz;
    Addr align;
  };

  struct Sym {
    Word name;
    Byte info;
    Byte other;
    Half shndx;
    Addr value;
    Addr size;
  };

  static constexpr unsigned int kRelTypeBits = 32;
};

// Forward declarations (see note.h).
struct ElfNote;
template <ElfData Data>
class ElfNoteSegment;

template <typename Addr>
constexpr auto kAddrBits = std::numeric_limits<typename Addr::value_type>::digits;

// The various ELF data structure layouts differ by class (32-bit vs 64-bit).
// But many use the same layout with certain fields being either 32 or 64 bits.
// The layouts that actually differ in field order and the like are defined by
// the Layout base class; the common-by-analogy layouts are defined here.
template <ElfClass Class = ElfClass::kNative, ElfData Data = ElfData::kNative>
struct Elf : private Layout<Class, Data> {
  static constexpr ElfClass kClass = Class;
  static constexpr ElfData kData = Data;

  using Layout<Class, Data>::kSwap;
  using typename Layout<Class, Data>::Byte;
  using typename Layout<Class, Data>::Half;
  using typename Layout<Class, Data>::Word;
  using typename Layout<Class, Data>::Xword;
  using typename Layout<Class, Data>::Addr;

  using size_type = typename Addr::value_type;

  using Addend = SignedField<size_type, kSwap>;

  static constexpr auto kAddressBits = kAddrBits<Addr>;

  using Note = ElfNote;
  using NoteSegment = ElfNoteSegment<kData>;

  struct Ehdr {
    constexpr bool Valid() const {
      return magic == kMagic &&                        // It's ELF at all,
             elfclass == Class && elfdata == Data &&   // of the right sort,
             ident_version == ElfVersion::kCurrent &&  // and passes basic
             version == ElfVersion::kCurrent &&        // sanity checks of
             ehsize == sizeof(Ehdr);                   // various sorts.
    }

    constexpr bool Loadable(ElfMachine target = ElfMachine::kNative) const {
      return Valid() && type == ElfType::kDyn && machine == target;
    }

    static constexpr Word kMagic{{'\x7f', 'E', 'L', 'F'}};

    // phnum has this value to indicate the real number of phdrs is too large
    // to fit and is instead stored in shdr[0].info.
    static inline const Half kPnXnum{0xffff};

    // These together make up the traditional unsigned char e_ident[16].
    Word magic;
    ElfClass elfclass;
    ElfData elfdata;
    ElfVersion ident_version;
    Byte osabi;
    Byte abiversion;
    Byte ident_pad[7];

    EnumField<ElfType, kSwap> type;
    EnumField<ElfMachine, kSwap> machine;
    EnumField<ElfVersion, kSwap, uint32_t> version;
    Addr entry;
    Addr phoff;
    Addr shoff;
    Word flags;
    Half ehsize;
    Half phentsize;
    Half phnum;
    Half shentsize;
    Half shnum;
    Half shstrndx;
  };

  using typename Layout<Class, Data>::Phdr;

  // This is not really used at runtime except for the kPnXnum protocol.
  // But it's useful to have all the values handy for diagnostic tools.
  struct Shdr {
    enum class Type : uint32_t {
      kNull = 0,
      kProgbits = 1,
      kSymtab = 2,
      kStrtab = 3,
      kRela = 4,
      kHash = 5,
      kDynamic = 6,
      kNote = 7,
      kNobits = 8,
      kRel = 9,
      kShlib = 10,
      kDynsym = 11,
      kInitArray = 14,
      kFiniArray = 15,
      kPreinitArray = 16,
      kGroup = 17,
      kSymtabShndx = 18,
      kGnuAttributes = 0x6ffffff5,
      kGnuHash = 0x6ffffff6,
      kGnuLiblist = 0x6ffffff7,
      kChecksum = 0x6ffffff8,
      kSunwMove = 0x6ffffffa,
      kSunwComdat = 0x6ffffffb,
      kSunwSyminfo = 0x6ffffffc,
      kGnuVerdef = 0x6ffffffd,
      kGnuVerneed = 0x6ffffffe,
      kGnuVersym = 0x6fffffff,
    };

    // These are individual bits OR'd together.
    enum Flags : uint32_t {
      kWrite = 1u << 0,
      kAlloc = 1u << 1,
      kExecinstr = 1u << 2,
      kMerge = 1u << 4,
      kStrings = 1u << 5,
      kInfoLink = 1u << 6,
      kLinkOrder = 1u << 7,
      kOsNonconforming = 1u << 8,
      kGroup = 1u << 9,
      kTls = 1u << 10,
      kCompressed = 1u << 11,
      kOrdered = 1u << 30,
      kExclude = 1u << 31,
    };

    Word name;
    EnumField<Type, kSwap> type;
    Addr flags;
    Addr addr;
    Addr offset;
    Addr size;
    Word link;
    Word info;
    Addr addralign;
    Addr entsize;
  };

  struct Dyn {
    EnumField<ElfDynTag, kSwap, size_type> tag;

    // Traditionally this was a union d_un of d_val and d_ptr, but both with
    // types that are just an address-sized unsigned integer.  Sometimes the
    // value is a "pointer" (relative to load bias) to some data structure.
    // Sometimes it's a byte size.  Sometimes it's an enum constant.
    Addr val;
  };

  struct Sym : public Layout<Class, Data>::Sym {
    using Layout<Class, Data>::Sym::Sym;
    using Layout<Class, Data>::Sym::operator=;

    // The layout differs completely by class, but the one-byte info field is
    // always encoded the same way.

    constexpr ElfSymBind bind() const { return static_cast<ElfSymBind>(this->info() >> 4); }

    constexpr ElfSymType type() const { return static_cast<ElfSymType>(this->info() & 0xf); }
  };

  struct Rel {
    constexpr uint32_t symndx() const { return info() >> kSymndxShift; }
    constexpr uint32_t type() const { return info() & kTypeMask; }

    static constexpr auto kSymndxShift = Layout<Class, Data>::kRelTypeBits;
    static constexpr auto kTypeMask = (size_type{1} << kSymndxShift) - 1;

    Addr offset;
    Addr info;
  };

  struct Rela : public Rel {
    SignedField<size_type, kSwap> addend;
  };
};

template <ElfData Data = ElfData::kNative>
using Elf32 = Elf<ElfClass::k32, Data>;

template <ElfData Data = ElfData::kNative>
using Elf64 = Elf<ElfClass::k64, Data>;

// This instantiates Template with Elf64<> and Elf32<> as parameters.
template <template <class...> typename Template>
using AllNativeFormats = Template<Elf64<>, Elf32<>>;

// This instantiates Template with each Elf variant as a parameter.
template <template <class...> typename Template>
using AllFormats = Template<Elf64<ElfData::k2Lsb>, Elf32<ElfData::k2Lsb>,  //
                            Elf64<ElfData::k2Msb>, Elf32<ElfData::k2Msb>>;

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LAYOUT_H_
