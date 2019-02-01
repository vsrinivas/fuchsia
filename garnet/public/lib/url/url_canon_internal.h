// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_URL_URL_CANON_INTERNAL_H_
#define LIB_URL_URL_CANON_INTERNAL_H_

// This file is intended to be included in another C++ file where the character
// types are defined. This allows us to write mostly generic code, but not have
// template bloat because everything is inlined when anybody calls any of our
// functions.

#include <stdlib.h>

#include "lib/fxl/logging.h"
#include "lib/url/url_canon.h"

namespace url {

// Character type handling -----------------------------------------------------

// Bits that identify different character types. These types identify different
// bits that are set for each 8-bit character in the kSharedCharTypeTable.
enum SharedCharTypes {
  // Characters that do not require escaping in queries. Characters that do
  // not have this flag will be escaped; see url_canon_query.cc
  CHAR_QUERY = 1,

  // Valid in the username/password field.
  CHAR_USERINFO = 2,

  // Valid in a IPv4 address (digits plus dot and 'x' for hex).
  CHAR_IPV4 = 4,

  // Valid in an ASCII-representation of a hex digit (as in %-escaped).
  CHAR_HEX = 8,

  // Valid in an ASCII-representation of a decimal digit.
  CHAR_DEC = 16,

  // Valid in an ASCII-representation of an octal digit.
  CHAR_OCT = 32,

  // Characters that do not require escaping in encodeURIComponent. Characters
  // that do not have this flag will be escaped; see url_util.cc.
  CHAR_COMPONENT = 64,
};

// This table contains the flags in SharedCharTypes for each 8-bit character.
// Some canonicalization functions have their own specialized lookup table.
// For those with simple requirements, we have collected the flags in one
// place so there are fewer lookup tables to load into the CPU cache.
//
// Using an unsigned char type has a small but measurable performance benefit
// over using a 32-bit number.
extern const unsigned char kSharedCharTypeTable[0x100];

// More readable wrappers around the character type lookup table.
inline bool IsCharOfType(unsigned char c, SharedCharTypes type) {
  return !!(kSharedCharTypeTable[c] & type);
}
inline bool IsQueryChar(unsigned char c) { return IsCharOfType(c, CHAR_QUERY); }
inline bool IsIPv4Char(unsigned char c) { return IsCharOfType(c, CHAR_IPV4); }
inline bool IsHexChar(unsigned char c) { return IsCharOfType(c, CHAR_HEX); }
inline bool IsComponentChar(unsigned char c) {
  return IsCharOfType(c, CHAR_COMPONENT);
}

// Appends the given string to the output, escaping characters that do not
// match the given |type| in SharedCharTypes.
void AppendStringOfType(const char* source, size_t length, SharedCharTypes type,
                        CanonOutput* output);

// Maps the hex numerical values 0x0 to 0xf to the corresponding ASCII digit
// that will be used to represent it.
URL_EXPORT extern const char kHexCharLookup[0x10];

// This lookup table allows fast conversion between ASCII hex letters and their
// corresponding numerical value. The 8-bit range is divided up into 8
// regions of 0x20 characters each. Each of the three character types (numbers,
// uppercase, lowercase) falls into different regions of this range. The table
// contains the amount to subtract from characters in that range to get at
// the corresponding numerical value.
//
// See HexDigitToValue for the lookup.
extern const char kCharToHexLookup[8];

// Assumes the input is a valid hex digit! Call IsHexChar before using this.
inline unsigned char HexCharToValue(unsigned char c) {
  return c - kCharToHexLookup[c / 0x20];
}

// Indicates if the given character is a dot or dot equivalent, returning the
// number of characters taken by it. This will be one for a literal dot, 3 for
// an escaped dot. If the character is not a dot, this will return 0.
inline size_t IsDot(const char* spec, size_t offset, size_t end) {
  if (spec[offset] == '.') {
    return 1;
  } else if (spec[offset] == '%' && offset + 3 <= end &&
             spec[offset + 1] == '2' &&
             (spec[offset + 2] == 'e' || spec[offset + 2] == 'E')) {
    // Found "%2e"
    return 3;
  }
  return 0;
}

// Returns the canonicalized version of the input character according to scheme
// rules. This is implemented alongside the scheme canonicalizer, and is
// required for relative URL resolving to test for scheme equality.
//
// Returns 0 if the input character is not a valid scheme character.
char CanonicalSchemeChar(char ch);

// Write a single character, escaped, to the output. This always escapes: it
// does no checking that thee character requires escaping.
// Escaping makes sense only 8 bit chars, so code works in all cases of
// input parameters (8/16bit).
template <typename UINCHAR, typename OUTCHAR>
inline void AppendEscapedChar(UINCHAR ch, CanonOutputT<OUTCHAR>* output) {
  output->push_back('%');
  output->push_back(kHexCharLookup[(ch >> 4) & 0xf]);
  output->push_back(kHexCharLookup[ch & 0xf]);
}

// UTF-8 functions ------------------------------------------------------------

// Reads one character in UTF-8 starting at |*begin| in |str| and places
// the decoded value into |*code_point|. If the character is valid, we will
// return true. If invalid, we'll return false and put the
// kUnicodeReplacementCharacter into |*code_point|.
//
// |*begin| will be updated to point to the last character consumed so it
// can be incremented in a loop and will be ready for the next character.
// (for a single-byte ASCII character, it will not be changed).
URL_EXPORT bool ReadUTFChar(const char* str, size_t* begin, size_t length,
                            unsigned* code_point_out);

// Generic To-UTF-8 converter. This will call the given append method for each
// character that should be appended, with the given output method. Wrappers
// are provided below for escaped and non-escaped versions of this.
//
// The char_value must have already been checked that it's a valid Unicode
// character.
template <class Output, void Appender(unsigned char, Output*)>
inline void DoAppendUTF8(unsigned char_value, Output* output) {
  if (char_value <= 0x7f) {
    Appender(static_cast<unsigned char>(char_value), output);
  } else if (char_value <= 0x7ff) {
    // 110xxxxx 10xxxxxx
    Appender(static_cast<unsigned char>(0xC0 | (char_value >> 6)), output);
    Appender(static_cast<unsigned char>(0x80 | (char_value & 0x3f)), output);
  } else if (char_value <= 0xffff) {
    // 1110xxxx 10xxxxxx 10xxxxxx
    Appender(static_cast<unsigned char>(0xe0 | (char_value >> 12)), output);
    Appender(static_cast<unsigned char>(0x80 | ((char_value >> 6) & 0x3f)),
             output);
    Appender(static_cast<unsigned char>(0x80 | (char_value & 0x3f)), output);
  } else if (char_value <= 0x10FFFF) {  // Max Unicode code point.
    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    Appender(static_cast<unsigned char>(0xf0 | (char_value >> 18)), output);
    Appender(static_cast<unsigned char>(0x80 | ((char_value >> 12) & 0x3f)),
             output);
    Appender(static_cast<unsigned char>(0x80 | ((char_value >> 6) & 0x3f)),
             output);
    Appender(static_cast<unsigned char>(0x80 | (char_value & 0x3f)), output);
  } else {
    // Invalid UTF-8 character (>20 bits).
    FXL_NOTREACHED();
  }
}

// Helper used by AppendUTF8Value below. We use an unsigned parameter so there
// are no funny sign problems with the input, but then have to convert it to
// a regular char for appending.
inline void AppendCharToOutput(unsigned char ch, CanonOutput* output) {
  output->push_back(static_cast<char>(ch));
}

// Writes the given character to the output as UTF-8. This does NO checking
// of the validity of the Unicode characters; the caller should ensure that
// the value it is appending is valid to append.
inline void AppendUTF8Value(unsigned char_value, CanonOutput* output) {
  DoAppendUTF8<CanonOutput, AppendCharToOutput>(char_value, output);
}

// Writes the given character to the output as UTF-8, escaping ALL
// characters (even when they are ASCII). This does NO checking of the
// validity of the Unicode characters; the caller should ensure that the value
// it is appending is valid to append.
inline void AppendUTF8EscapedValue(unsigned char_value, CanonOutput* output) {
  DoAppendUTF8<CanonOutput, AppendEscapedChar>(char_value, output);
}

// UTF-16 functions -----------------------------------------------------------

// Reads one character in UTF-16 starting at |*begin| in |str| and places
// the decoded value into |*code_point|. If the character is valid, we will
// return true. If invalid, we'll return false and put the
// kUnicodeReplacementCharacter into |*code_point|.
//
// |*begin| will be updated to point to the last character consumed so it
// can be incremented in a loop and will be ready for the next character.
// (for a single-16-bit-word character, it will not be changed).
URL_EXPORT bool ReadUTFChar(const uint16_t* str, size_t* begin, size_t length,
                            unsigned* code_point_out);

// Equivalent to U16_APPEND_UNSAFE in ICU but uses our output method.
inline void AppendUTF16Value(unsigned code_point,
                             CanonOutputT<uint16_t>* output) {
  if (code_point > 0xffff) {
    output->push_back(static_cast<uint16_t>((code_point >> 10) + 0xd7c0));
    output->push_back(static_cast<uint16_t>((code_point & 0x3ff) | 0xdc00));
  } else {
    output->push_back(static_cast<uint16_t>(code_point));
  }
}

// Escaping functions ---------------------------------------------------------

// Writes the given character to the output as UTF-8, escaped. Call this
// function only when the input is wide. Returns true on success. Failure
// means there was some problem with the encoding, we'll still try to
// update the |*begin| pointer and add a placeholder character to the
// output so processing can continue.
//
// We will append the character starting at ch[begin] with the buffer ch
// being |length|. |*begin| will be updated to point to the last character
// consumed (we may consume more than one for UTF-16) so that if called in
// a loop, incrementing the pointer will move to the next character.
//
// Every single output character will be escaped. This means that if you
// give it an ASCII character as input, it will be escaped. Some code uses
// this when it knows that a character is invalid according to its rules
// for validity. If you don't want escaping for ASCII characters, you will
// have to filter them out prior to calling this function.
//
// Assumes that ch[begin] is within range in the array, but does not assume
// that any following characters are.
inline bool AppendUTF8EscapedChar(const uint16_t* str, size_t* begin,
                                  size_t length, CanonOutput* output) {
  // UTF-16 input. ReadUTFChar will handle invalid characters for us and give
  // us the kUnicodeReplacementCharacter, so we don't have to do special
  // checking after failure, just pass through the failure to the caller.
  unsigned char_value;
  bool success = ReadUTFChar(str, begin, length, &char_value);
  AppendUTF8EscapedValue(char_value, output);
  return success;
}

// Handles UTF-8 input. See the wide version above for usage.
inline bool AppendUTF8EscapedChar(const char* str, size_t* begin, size_t length,
                                  CanonOutput* output) {
  // ReadUTF8Char will handle invalid characters for us and give us the
  // kUnicodeReplacementCharacter, so we don't have to do special checking
  // after failure, just pass through the failure to the caller.
  unsigned ch;
  bool success = ReadUTFChar(str, begin, length, &ch);
  AppendUTF8EscapedValue(ch, output);
  return success;
}

// Given a '%' character at |*begin| in the string |spec|, this will decode
// the escaped value and put it into |*unescaped_value| on success (returns
// true). On failure, this will return false, and will not write into
// |*unescaped_value|.
//
// |*begin| will be updated to point to the last character of the escape
// sequence so that when called with the index of a for loop, the next time
// through it will point to the next character to be considered. On failure,
// |*begin| will be unchanged.
inline bool Is8BitChar(char c) {
  return true;  // this case is specialized to avoid a warning
}

inline bool Is8BitChar(uint16_t c) { return c <= 255; }

template <typename CHAR>
inline bool DecodeEscaped(const CHAR* spec, size_t* begin, size_t end,
                          unsigned char* unescaped_value) {
  if (*begin + 3 > end || !Is8BitChar(spec[*begin + 1]) ||
      !Is8BitChar(spec[*begin + 2])) {
    // Invalid escape sequence because there's not enough room, or the
    // digits are not ASCII.
    return false;
  }

  unsigned char first = static_cast<unsigned char>(spec[*begin + 1]);
  unsigned char second = static_cast<unsigned char>(spec[*begin + 2]);
  if (!IsHexChar(first) || !IsHexChar(second)) {
    // Invalid hex digits, fail.
    return false;
  }

  // Valid escape sequence.
  *unescaped_value = (HexCharToValue(first) << 4) + HexCharToValue(second);
  *begin += 2;
  return true;
}

// Appends the given substring to the output, escaping "some" characters that
// it feels may not be safe. It assumes the input values are all contained in
// 8-bit although it allows any type.
//
// This is used in error cases to append invalid output so that it looks
// approximately correct. Non-error cases should not call this function since
// the escaping rules are not guaranteed!
void AppendInvalidNarrowString(const char* spec, size_t begin, size_t end,
                               CanonOutput* output);
void AppendInvalidNarrowString(const uint16_t* spec, size_t begin, size_t end,
                               CanonOutput* output);

// Misc canonicalization helpers ----------------------------------------------

// Converts between UTF-8 and UTF-16, returning true on successful conversion.
// The output will be appended to the given canonicalizer output (so make sure
// it's empty if you want to replace).
//
// On invalid input, this will still write as much output as possible,
// replacing the invalid characters with the "invalid character". It will
// return false in the failure case, and the caller should not continue as
// normal.
URL_EXPORT bool ConvertUTF16ToUTF8(const uint16_t* input, size_t input_len,
                                   CanonOutput* output);
URL_EXPORT bool ConvertUTF8ToUTF16(const char* input, size_t input_len,
                                   CanonOutputT<uint16_t>* output);

// Converts from UTF-16 to 8-bit using the character set converter. If the
// converter is NULL, this will use UTF-8.
void ConvertUTF16ToQueryEncoding(const uint16_t* input, const Component& query,
                                 CharsetConverter* converter,
                                 CanonOutput* output);

// Implemented in url_canon_path.cc, these are required by the relative URL
// resolver as well, so we declare them here.
bool CanonicalizePartialPath(const char* spec, const Component& path,
                             size_t path_begin_in_output, CanonOutput* output);

// Implementations of Windows' int-to-string conversions
int IntToString(int value, char* buffer, size_t size_in_chars, int radix);

// Secure template overloads for these functions
template <size_t N>
inline int IntToString(int value, char (&buffer)[N], int radix) {
  return IntToString(value, buffer, N, radix);
}

// _strtoui64 and strtoull behave the same
inline unsigned long long _strtoui64(const char* nptr, char** endptr,
                                     int base) {
  return strtoull(nptr, endptr, base);
}

// IDN ------------------------------------------------------------------------

// Converts the Unicode input representing a hostname to ASCII using IDN rules.
// The output must fall in the ASCII range, but will be encoded in UTF-16.
//
// On success, the output will be filled with the ASCII host name and it will
// return true. Unlike most other canonicalization functions, this assumes that
// the output is empty. The beginning of the host will be at offset 0, and
// the length of the output will be set to the length of the new host name.
//
// On error, returns false. The output in this case is undefined.
URL_EXPORT bool IDNToASCII(const uint16_t* src, size_t src_len,
                           CanonOutputW* output);
}  // namespace url

#endif  // LIB_URL_URL_CANON_INTERNAL_H_
