// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// No header inclusion guards _sic_ as this may be re-included with
// different X macro arguments.

#if !defined(TOKEN)
#define TOKEN(Name)
#endif

#if !defined(TOKEN_SUBKIND)
#define TOKEN_SUBKIND(Name, Spelling)
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

TOKEN(At)
TOKEN(Dot)
TOKEN(Comma)
TOKEN(Semicolon)
TOKEN(Colon)
TOKEN(Question)
TOKEN(Equal)
TOKEN(Ampersand)
TOKEN(Arrow)
TOKEN(Pipe)

// Token subkinds
TOKEN_SUBKIND(As, "as")
TOKEN_SUBKIND(Alias, "alias")
TOKEN_SUBKIND(Library, "library")
TOKEN_SUBKIND(Using, "using")

TOKEN_SUBKIND(Array, "array")
TOKEN_SUBKIND(Request, "request")
TOKEN_SUBKIND(String, "string")
TOKEN_SUBKIND(Vector, "vector")

// TOKEN_SUBKIND(Max, "MAX")
// MAX is not a real keyword, but it effectively works like one. It can be used
// as vector<T>:MAX or string:MAX. See Library::ResolveSizeBound in flat_ast.cc.

TOKEN_SUBKIND(Bits, "bits")
TOKEN_SUBKIND(Const, "const")
TOKEN_SUBKIND(Enum, "enum")
TOKEN_SUBKIND(Protocol, "protocol")
TOKEN_SUBKIND(Resource, "resource")
TOKEN_SUBKIND(ResourceDefinition, "resource_definition")
TOKEN_SUBKIND(Service, "service")
TOKEN_SUBKIND(Strict, "strict")
TOKEN_SUBKIND(Struct, "struct")
TOKEN_SUBKIND(Table, "table")
TOKEN_SUBKIND(Flexible, "flexible")
TOKEN_SUBKIND(Type, "type")
TOKEN_SUBKIND(Union, "union")
TOKEN_SUBKIND(XUnion, "xunion")

TOKEN_SUBKIND(Error, "error")

TOKEN_SUBKIND(True, "true")
TOKEN_SUBKIND(False, "false")

TOKEN_SUBKIND(Compose, "compose")
TOKEN_SUBKIND(Reserved, "reserved")
TOKEN_SUBKIND(Properties, "properties")

#undef TOKEN_SUBKIND
#undef TOKEN
