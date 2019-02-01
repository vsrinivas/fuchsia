// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_INDENT_H_
#define LIB_FOSTR_INDENT_H_

#include <iomanip>
#include <ostream>

namespace fostr {

// Use these otuput manipulators to generate indented output:
//
// os << fostr::IdentBy(2); // Default is 4 spaces.
// os << "items:";
// os << fostr::Indent;
// os << fostr::NewLine << "item 1";
// os << fostr::NewLine << "item 2";
// os << fostr::Indent;
// os << fostr::NewLine << "item 2A";
// os << fostr::NewLine << "item 2B";
// os << fostr::Outdent;
// os << fostr::NewLine << "item 3";
// os << fostr::Outdent;
//
// This inserts:
//
// items:\n
//   item 1\n
//   item 2\n
//     item 2A\n
//     item 2B\n
//   item 3
//
// A useful pattern for nested output is for any given ostream operator<<
// overload to assume there is already text on the current line and to refrain
// from terminating the last line. In general, this means that fostr::NewLine
// prefixes text that needs to be on a new lines, and lines are not terminated.

namespace internal {

struct IndentLevel {
  __attribute__((visibility("default"))) static long& Value(std::ostream& os);
  IndentLevel(long level) : level_(level) {}
  long level_;
};

struct IndentBy {
  __attribute__((visibility("default"))) static long& Value(std::ostream& os);
  IndentBy(long spaces) : spaces_(spaces) {}
  long spaces_;
};

inline std::ostream& operator<<(std::ostream& os, const IndentLevel& value) {
  IndentLevel::Value(os) = value.level_;
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const IndentBy& value) {
  IndentBy::Value(os) = value.spaces_;
  return os;
}

}  // namespace internal

// Inserts spaces to begin an indented line. The number of spaces inserted is
// the product of the indent level and the 'indent by' setting.
// e.g. os << fostr::BeginLine << "appears after indentation";
inline std::ostream& BeginLine(std::ostream& os) {
  return os << std::setw(internal::IndentLevel::Value(os) *
                         internal::IndentBy::Value(os))
            << std::setfill(' ') << "" << std::setw(0);
}

// Inserts a newline and a BeginLine.
// e.g. os << fostr::NewLine << "appears on a new indented line";
inline std::ostream& NewLine(std::ostream& os) {
  return os << "\n" << BeginLine;
}

// As an ostream manipulator, increments the indentation level.
// e.g. os << fostr::Indent;
inline std::ostream& Indent(std::ostream& os) {
  ++internal::IndentLevel::Value(os);
  return os;
}

// As an ostream manipulator, decrements the indentation level.
// e.g. os << fostr::Outdent;
inline std::ostream& Outdent(std::ostream& os) {
  --internal::IndentLevel::Value(os);
  return os;
}

// As an ostream manipulator, sets the indentation level.
// e.g. os << fostr::IdentLevel(3);
inline internal::IndentLevel IdentLevel(long level) {
  return internal::IndentLevel(level);
}

// Gets the current indentation level.
// e.g. long current_level = fostr::GetIdentLevel(os);
inline long GetIdentLevel(std::ostream& os) {
  return internal::IndentLevel::Value(os);
}

// As an ostream manipulator, sets the number of spaces to indent by for each
// indentation level. The default 'indent by' value is 4.
// e.g. os << fostr::IdentBy(2);
inline internal::IndentBy IdentBy(long spaces) {
  return internal::IndentBy(spaces);
}

}  // namespace fostr

#endif  // LIB_FOSTR_INDENT_H_
