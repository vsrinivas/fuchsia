// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_JOB_ARCHIVE_H_
#define SRC_LIB_ZXDUMP_JOB_ARCHIVE_H_

#include <string_view>

namespace zxdump {

// Standard `ar` archive format (ar_hdr and ar_* are traditional names).

// The archive starts with a header that's simply this fixed magic string.
inline constexpr std::string_view kArchiveMagic{"!<arch>\n"};

// After the archive header follow any number of archive members.  Each archive
// member has a header in this format, followed by the contents.  The exact
// size of the contents is encoded in ar_size.  Odd-sized members are padded
// with a single `\n`.  The order of archive members is not significant in
// general, but some special archive members with reserved names must always
// appear first in the archive if they appear at all.  (In job archives there
// is a canonical order the dump-writer produces consistently, but the
// dump-reader handles members in any order.  Merely repacking an archive with
// `ar` is likely to reorder members.)
struct ar_hdr {
  // The ar_fmag field must always have the same constant value (kMagic).
  constexpr bool valid() const {
    std::string_view magic{ar_fmag, sizeof(ar_fmag)};
    static_assert(sizeof(ar_fmag) == kMagic.size());
    return magic == kMagic;
  }

  // All ar_name strings starting with "/" are reserved for special uses.
  // A string in the format "/%u" is a reference to the long name table.
  static constexpr std::string_view kLongNamePrefix{"/"};

  // Other reserved names identify special archive members.  These special
  // member headers have normal ar_size and ar_fmag fields but may have just
  // all ' ' padding for all other fields rather than the normal encoding.

  // The special archive member with name "/" must be the first member in the
  // archive if it's present at all.  It's the archive symbol table, which is
  // only used for static linking archives.  It's not usually present in a job
  // archive and will be ignored.
  static constexpr std::string_view kSymbolTableName{"/"};

  // The special archive member with name "//" must be first in the archive if
  // it's present at all (or the second member after the symbol table member
  // if the symbol table is present).  This is necessary to represent member
  // file names longer than the ar_name field.  The contents are a sequence of
  // file name strings, each ending with "/\n".  A member header name of "/%u"
  // is replaced with the string at the `%u` byte offset into this long name
  // table (not including its "/\n" terminator).  The long name table is
  // optional in archives generally, and when there is a long name table it's
  // optional to use it for names that do fit in the ar_name size.  (Job
  // archives always have a long name table because most "note" file names are
  // too long for ar_name.  They may or may not use it for other members.  The
  // canonical dump-writer always uses the long table for the "note" files and
  // never uses it for embedded dump files so it can do streaming output.  But
  // merely repacking an archive with `ar` might change this.)
  static constexpr std::string_view kNameTableName{"//"};
  static constexpr std::string_view kNameTableTerminator{"/\n"};

  static constexpr std::string_view kMagic{"`\n"};  // ar_fmag value

  // All fields are left-justified and padded with spaces, no NUL terminator.
  char ar_name[16];  // File path (relative) or special case starting with '/'.
  char ar_date[12];  // decimal time_t (seconds since 1970 UTC)
  char ar_uid[6];    // decimal uid_t
  char ar_gid[6];    // decimal gid_t
  char ar_mode[8];   // octal mode_t (0777 bits only)
  char ar_size[10];  // decimal size_t / off_t
  char ar_fmag[2];   // Must be "`\n" (ar_hdr::kMagic).
};

// Any nonempty archive will be at least this big.
inline constexpr auto kMinimumArchive = kArchiveMagic.size() + sizeof(ar_hdr);

// Zircon job archive format.  Archive member file names with these prefixes
// are "note" files that contain Zircon format data about the job.  Other
// member files are embedded dumps or random attachments.

// The ZX_INFO_* value is encoded in decimal after this and a dot.
inline constexpr std::string_view kJobInfoName{"ZirconJobInfo"};

// The ZX_PROP_* value is encoded in decimal after this and a dot.
inline constexpr std::string_view kJobPropertyName{"ZirconJobProperty"};

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_JOB_ARCHIVE_H_
