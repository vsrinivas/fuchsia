// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// No header inclusion guards _sic_ as this may be re-included with
// different X macro arguments.

#if !defined(TOKEN)
#define TOKEN(Name)
#endif

#if !defined(KEYWORD)
#define KEYWORD(Name, Spelling)
#endif

// Control and whitespace
TOKEN(NotAToken)
TOKEN(EndOfFile)
TOKEN(Comment)
TOKEN(DocComment)

// Identifiers and constants
TOKEN(Identifier)
TOKEN(NumericLiteral)
TOKEN(StringLiteral)

// Punctuation.
TOKEN(LeftParen)
TOKEN(RightParen)
TOKEN(LeftSquare)
TOKEN(RightSquare)
TOKEN(LeftCurly)
TOKEN(RightCurly)
TOKEN(LeftAngle)
TOKEN(RightAngle)

TOKEN(Dot)
TOKEN(Comma)
TOKEN(Semicolon)
TOKEN(Colon)
TOKEN(Question)
TOKEN(Equal)
TOKEN(Ampersand)
TOKEN(Arrow)
TOKEN(Pipe)

// Keywords
KEYWORD(As, "as")
KEYWORD(Library, "library")
KEYWORD(Using, "using")

KEYWORD(Array, "array")
KEYWORD(Handle, "handle")
KEYWORD(Request, "request")
KEYWORD(String, "string")
KEYWORD(Vector, "vector")

// KEYWORD(Max, "MAX")
// MAX is not a real keyword, but it effectively works like one. It can be used
// as vector<T>:MAX or string:MAX. See Library::ResolveSizeBound in flat_ast.cc.

KEYWORD(Bits, "bits")
KEYWORD(Const, "const")
KEYWORD(Enum, "enum")
KEYWORD(Protocol, "protocol")
KEYWORD(Service, "service")
KEYWORD(Strict, "strict")
KEYWORD(Struct, "struct")
KEYWORD(Table, "table")
KEYWORD(Flexible, "experimental_flexible")
KEYWORD(Union, "union")
KEYWORD(XUnion, "xunion")

KEYWORD(Error, "error")

KEYWORD(True, "true")
KEYWORD(False, "false")

KEYWORD(Reserved, "reserved")

#undef KEYWORD
#undef TOKEN
