// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_LANG_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_LANG_H_

namespace zxdb {

enum class DwarfLang : int {
  kNone = 0,
  kC89 = 0x01,
  kC = 0x02,
  kAda83 = 0x03,
  kCpp = 0x04,
  kCobol74 = 0x05,
  kCobol85 = 0x06,
  kFortran77 = 0x07,
  kFortran90 = 0x08,
  kPascal83 = 0x09,
  kModula2 = 0x0a,
  kJava = 0x0b,
  kC99 = 0x0c,
  kAda95 = 0x0d,
  kFortran95 = 0x0e,
  kPLI = 0x0f,
  kObjC = 0x10,
  kObjCpp = 0x11,
  kUPC = 0x12,
  kD = 0x13,
  kPython = 0x14,
  kOpenCL = 0x15,
  kGo = 0x16,
  kModula3 = 0x17,
  kHaskell = 0x18,
  kCpp03 = 0x19,
  kCpp11 = 0x1a,
  kOCaml = 0x1b,
  kRust = 0x1c,
  kC11 = 0x1d,
  kSwift = 0x1e,
  kJulia = 0x1f,
  kDylan = 0x20,
  kCpp14 = 0x21,
  kFortran03 = 0x22,
  kFortran08 = 0x23,
  kRenderScript = 0x24,
  kBLISS = 0x25,

  kLast
};

// Returns true for any version of C, C++, and Objective C.
bool DwarfLangIsCFamily(DwarfLang lang);

const char* DwarfLangToString(DwarfLang lang);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_LANG_H_
