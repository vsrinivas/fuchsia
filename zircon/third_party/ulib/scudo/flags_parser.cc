//===-- flags_parser.cc -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flags_parser.h"
#include "common.h"
#include "report.h"

#include <string.h>

namespace scudo {

class UnknownFlagsRegistry {
  static const uptr MaxUnknownFlags = 16;
  const char *UnknownFlagsNames[MaxUnknownFlags];
  uptr NumberOfUnknownFlags;

public:
  void add(const char *Name) {
    CHECK_LT(NumberOfUnknownFlags, MaxUnknownFlags);
    UnknownFlagsNames[NumberOfUnknownFlags++] = Name;
  }

  void report() {
    if (!NumberOfUnknownFlags)
      return;
    Printf("Scudo WARNING: found %d unrecognized flag(s):\n",
           NumberOfUnknownFlags);
    for (uptr I = 0; I < NumberOfUnknownFlags; ++I)
      Printf("    %s\n", UnknownFlagsNames[I]);
    NumberOfUnknownFlags = 0;
  }
};
static UnknownFlagsRegistry UnknownFlags;

void reportUnrecognizedFlags() { UnknownFlags.report(); }

FlagsAllocator FlagParser::Alloc;

void *FlagsAllocator::allocate(uptr Size) {
  Size = roundUpTo(Size, 8);
  if (AllocatedEnd - AllocatedCurrent < Size) {
    const uptr SizeToAllocate = Max(Size, getPageSizeCached());
    AllocatedCurrent =
        reinterpret_cast<uptr>(map(nullptr, SizeToAllocate, "scudo:flags"));
    AllocatedEnd = AllocatedCurrent + SizeToAllocate;
  }
  DCHECK(AllocatedEnd - AllocatedCurrent >= Size);
  void *P = reinterpret_cast<void *>(AllocatedCurrent);
  AllocatedCurrent += Size;
  return P;
}

char *FlagParser::duplicateString(const char *S, uptr N) {
  const uptr Length = strnlen(S, N);
  char *NewS = reinterpret_cast<char *>(Alloc.allocate(Length + 1));
  memcpy(NewS, S, Length);
  NewS[Length] = 0;
  return NewS;
}

void FlagParser::printFlagDescriptions() {}

void FlagParser::reportFatalError(const char *Error) { reportError(Error); }

bool FlagParser::isSpace(char C) {
  return C == ' ' || C == ',' || C == ':' || C == '\n' || C == '\t' ||
         C == '\r';
}

void FlagParser::skipWhitespace() {
  while (isSpace(Buffer[Pos]))
    ++Pos;
}

void FlagParser::parseFlag() {
  const uptr NameStart = Pos;
  while (Buffer[Pos] != 0 && Buffer[Pos] != '=' && !isSpace(Buffer[Pos]))
    ++Pos;
  if (Buffer[Pos] != '=')
    reportFatalError("expected '='");
  char *Name = duplicateString(Buffer + NameStart, Pos - NameStart);

  const uptr ValueStart = ++Pos;
  char *Value;
  if (Buffer[Pos] == '\'' || Buffer[Pos] == '"') {
    char quote = Buffer[Pos++];
    while (Buffer[Pos] != 0 && Buffer[Pos] != quote)
      ++Pos;
    if (Buffer[Pos] == 0)
      reportFatalError("unterminated string");
    Value = duplicateString(Buffer + ValueStart + 1, Pos - ValueStart - 1);
    ++Pos; // consume the closing quote
  } else {
    while (Buffer[Pos] != 0 && !isSpace(Buffer[Pos]))
      ++Pos;
    if (Buffer[Pos] != 0 && !isSpace(Buffer[Pos]))
      reportFatalError("expected separator or eol");
    Value = duplicateString(Buffer + ValueStart, Pos - ValueStart);
  }

  if (!runHandler(Name, Value))
    reportFatalError("Flag parsing failed.");
}

void FlagParser::parseFlags() {
  while (true) {
    skipWhitespace();
    if (Buffer[Pos] == 0)
      break;
    parseFlag();
  }
}

void FlagParser::parseString(const char *S) {
  if (!S)
    return;
  // Backup current parser state to allow nested parseString() calls.
  const char *OldBuffer = Buffer;
  const uptr OldPos = Pos;
  Buffer = S;
  Pos = 0;

  parseFlags();

  Buffer = OldBuffer;
  Pos = OldPos;
}

bool FlagParser::runHandler(const char *Name, const char *Value) {
  for (u32 i = 0; i < NumberOfFlags; ++i) {
    if (strcmp(Name, Flags[i].Name) == 0)
      return Flags[i].Handler->parse(Value);
  }
  // Unrecognized flag. This is not a fatal error, we may print a warning later.
  UnknownFlags.add(Name);
  return true;
}

void FlagParser::registerHandler(const char *Name, FlagHandlerBase *Handler,
                                 const char *Desc) {
  CHECK_LT(NumberOfFlags, MaxFlags);
  Flags[NumberOfFlags].Name = Name;
  Flags[NumberOfFlags].Desc = Desc;
  Flags[NumberOfFlags].Handler = Handler;
  ++NumberOfFlags;
}

FlagParser::FlagParser() : NumberOfFlags(0), Buffer(nullptr), Pos(0) {
  Flags = reinterpret_cast<Flag *>(Alloc.allocate(sizeof(Flag) * MaxFlags));
}

} // namespace scudo
