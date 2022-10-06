// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"strings"

	cpp "go.fuchsia.dev/fuchsia/tools/fidl/gidl/cpp"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type HandleRepr int

const (
	_ = iota
	HandleReprDisposition
	HandleReprInfo
	HandleReprRaw
)

func typeNameImpl(decl gidlmixer.Declaration, ignoreNullable bool) string {
	switch decl := decl.(type) {
	case gidlmixer.PrimitiveDeclaration:
		return primitiveTypeName(decl.Subtype())
	case *gidlmixer.StringDecl:
		return "fidl::StringView"
	case *gidlmixer.StructDecl:
		if !ignoreNullable && decl.IsNullable() {
			return fmt.Sprintf("fidl::ObjectView<%s>", declName(decl))
		}
		return declName(decl)
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("fidl::Array<%s, %d>", typeName(decl.Elem()), decl.Size())
	case *gidlmixer.VectorDecl:
		return fmt.Sprintf("fidl::VectorView<%s>", typeName(decl.Elem()))
	case *gidlmixer.HandleDecl:
		switch decl.Subtype() {
		case fidlgen.HandleSubtypeNone:
			return "zx::handle"
		case fidlgen.HandleSubtypeChannel:
			return "zx::channel"
		case fidlgen.HandleSubtypeEvent:
			return "zx::event"
		default:
			panic(fmt.Sprintf("Handle subtype not supported %s", decl.Subtype()))
		}
	case *gidlmixer.ClientEndDecl:
		return fmt.Sprintf("fidl::ClientEnd<%s>", cpp.EndpointDeclName(decl))
	case *gidlmixer.ServerEndDecl:
		return fmt.Sprintf("fidl::ServerEnd<%s>", cpp.EndpointDeclName(decl))
	case *gidlmixer.UnionDecl:
		if !ignoreNullable && decl.IsNullable() {
			return fmt.Sprintf("fidl::WireOptional<%s>", declName(decl))
		}
		return declName(decl)
	case gidlmixer.NamedDeclaration:
		return declName(decl)
	default:
		panic("unhandled case")
	}
}

func typeName(decl gidlmixer.Declaration) string {
	return typeNameImpl(decl, false)
}

func typeNameIgnoreNullable(decl gidlmixer.Declaration) string {
	return typeNameImpl(decl, true)
}

func declName(decl gidlmixer.NamedDeclaration) string {
	// Note: only works for domain objects (not protocols & services)
	parts := strings.SplitN(decl.Name(), "/", 2)
	return fmt.Sprintf("%s::wire::%s", strings.ReplaceAll(parts[0], ".", "_"), fidlgen.ToUpperCamelCase(parts[1]))
}

func ConformanceType(gidlTypeString string) string {
	// Note: only works for domain objects (not protocols & services)
	return "test_conformance::wire::" + fidlgen.ToUpperCamelCase(gidlTypeString)
}

func LlcppErrorCode(code gidlir.ErrorCode) string {
	if code == gidlir.TooFewBytesInPrimaryObject || code == gidlir.TooFewBytes || code == gidlir.EnvelopeBytesExceedMessageLength {
		return "ZX_ERR_BUFFER_TOO_SMALL"
	}
	// TODO(fxbug.dev/35381) Implement different codes for different FIDL error cases.
	return "ZX_ERR_INVALID_ARGS"
}
