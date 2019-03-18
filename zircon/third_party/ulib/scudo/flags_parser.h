//===-- flags_parser.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_FLAGS_PARSER_H_
#define SCUDO_FLAGS_PARSER_H_

#include "report.h"
#include "string_utils.h"

#include <stddef.h>
#include <stdlib.h>

namespace scudo {

class FlagHandlerBase {
public:
  virtual bool parse(UNUSED const char *Value) { return false; }
  virtual ~FlagHandlerBase() = default;
  void operator delete(void *) {}
  void operator delete(void *, size_t) {}
};

template <typename T> class FlagHandler : public FlagHandlerBase {
  T *FlagValue;

public:
  explicit FlagHandler(T *Value) : FlagValue(Value) {}
  bool parse(const char *Value) final;
  // Even though we are using a placement new, delete still needs to be defined.
  void operator delete(void *) {}
  void operator delete(void *, size_t) {}
  ~FlagHandler() final = default;
};

INLINE bool parseBool(const char *Value, bool *b) {
  if (strcmp(Value, "0") == 0 || strcmp(Value, "no") == 0 ||
      strcmp(Value, "false") == 0) {
    *b = false;
    return true;
  }
  if (strcmp(Value, "1") == 0 || strcmp(Value, "yes") == 0 ||
      strcmp(Value, "true") == 0) {
    *b = true;
    return true;
  }
  return false;
}

template <> INLINE bool FlagHandler<bool>::parse(const char *Value) {
  if (parseBool(Value, FlagValue))
    return true;
  reportInvalidFlag("bool", Value);
  return false;
}

template <> INLINE bool FlagHandler<const char *>::parse(const char *Value) {
  *FlagValue = Value;
  return true;
}

template <> INLINE bool FlagHandler<int>::parse(const char *Value) {
  char *ValueEnd;
  *FlagValue = static_cast<int>(strtol(Value, &ValueEnd, 10));
  const bool Ok = *ValueEnd == 0;
  if (!Ok)
    reportInvalidFlag("int", Value);
  return Ok;
}

template <> INLINE bool FlagHandler<uptr>::parse(const char *Value) {
  char *ValueEnd;
  *FlagValue = static_cast<uptr>(strtoull(Value, &ValueEnd, 10));
  const bool Ok = *ValueEnd == 0;
  if (!Ok)
    reportInvalidFlag("uptr", Value);
  return Ok;
}

// Minimal map based allocator. Memory is never unmaped.
class FlagsAllocator {
public:
  // Requires an external lock.
  void *allocate(uptr Size);

private:
  uptr AllocatedEnd;
  uptr AllocatedCurrent;
};

class FlagParser {
  static const int MaxFlags = 32;
  struct Flag {
    const char *Name;
    const char *Desc;
    FlagHandlerBase *Handler;
  } * Flags;
  u32 NumberOfFlags;

  const char *Buffer;
  uptr Pos;

public:
  FlagParser();
  void registerHandler(const char *Name, FlagHandlerBase *Handler,
                       const char *Desc);
  void parseString(const char *S);
  void printFlagDescriptions();

  static FlagsAllocator Alloc;

private:
  void reportFatalError(const char *Error);
  bool isSpace(char C);
  void skipWhitespace();
  void parseFlags();
  void parseFlag();
  bool runHandler(const char *Name, const char *Value);
  char *duplicateString(const char *S, uptr N);
};

template <typename T>
static void registerFlag(FlagParser *Parser, const char *Name, const char *Desc,
                         T *Var) {
  FlagHandler<T> *Handler = new (FlagParser::Alloc) FlagHandler<T>(Var);
  Parser->registerHandler(Name, Handler, Desc);
}

void reportUnrecognizedFlags();

} // namespace scudo

INLINE void *operator new(size_t Size, scudo::FlagsAllocator &Alloc) {
  return Alloc.allocate(Size);
}

#endif // SCUDO_FLAGS_PARSER_H_
