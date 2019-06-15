// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_lang.h"

namespace zxdb {

bool DwarfLangIsCFamily(DwarfLang lang) {
  // clang-format off
  return lang == DwarfLang::kC89 ||
         lang == DwarfLang::kC ||
         lang == DwarfLang::kCpp ||
         lang == DwarfLang::kC99 ||
         lang == DwarfLang::kObjC ||
         lang == DwarfLang::kObjCpp ||
         lang == DwarfLang::kCpp03 ||
         lang == DwarfLang::kCpp11 ||
         lang == DwarfLang::kC11 ||
         lang == DwarfLang::kCpp14;
  // clang-format on
}

const char* DwarfLangToString(DwarfLang lang) {
  // clang-format off
  switch (lang) {
    case DwarfLang::kNone: return "None";
    case DwarfLang::kC89: return "C89";
    case DwarfLang::kC: return "C";
    case DwarfLang::kAda83: return "Ada83";
    case DwarfLang::kCpp: return "C++";
    case DwarfLang::kCobol74: return "Cobol74";
    case DwarfLang::kCobol85: return "Cobol85";
    case DwarfLang::kFortran77: return "Fortran77";
    case DwarfLang::kFortran90: return "Fortran90";
    case DwarfLang::kPascal83: return "Pascal83";
    case DwarfLang::kModula2: return "Modula2";
    case DwarfLang::kJava: return "Java";
    case DwarfLang::kC99: return "C99";
    case DwarfLang::kAda95: return "Ada95";
    case DwarfLang::kFortran95: return "Fortran95";
    case DwarfLang::kPLI: return "PLI";
    case DwarfLang::kObjC: return "ObjC";
    case DwarfLang::kObjCpp: return "ObjC++";
    case DwarfLang::kUPC: return "UPC";
    case DwarfLang::kD: return "D";
    case DwarfLang::kPython: return "Python";
    case DwarfLang::kOpenCL: return "OpenCL";
    case DwarfLang::kGo: return "Go";
    case DwarfLang::kModula3: return "Modula3";
    case DwarfLang::kHaskell: return "Haskell";
    case DwarfLang::kCpp03: return "C++03";
    case DwarfLang::kCpp11: return "C++11";
    case DwarfLang::kOCaml: return "OCaml";
    case DwarfLang::kRust: return "Rust";
    case DwarfLang::kC11: return "C11";
    case DwarfLang::kSwift: return "Swift";
    case DwarfLang::kJulia: return "Julia";
    case DwarfLang::kDylan: return "Dylan";
    case DwarfLang::kCpp14: return "C++14";
    case DwarfLang::kFortran03: return "Fortran03";
    case DwarfLang::kFortran08: return "Fortran08";
    case DwarfLang::kRenderScript: return "RenderScript";
    case DwarfLang::kBLISS: return "BLISS";
    default: return "<Unknown>";
  }
  // clang-format on
}

}  // namespace zxdb
