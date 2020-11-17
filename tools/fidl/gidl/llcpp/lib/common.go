// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"strings"

	gidlmixer "go.fuchsia.dev/fuchsia/tools/fidl/gidl/mixer"
	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func typeNameImpl(decl gidlmixer.Declaration, ignoreNullable bool) string {
	switch decl := decl.(type) {
	case gidlmixer.PrimitiveDeclaration:
		return primitiveTypeName(decl.Subtype())
	case *gidlmixer.StringDecl:
		return "fidl::StringView"
	case *gidlmixer.StructDecl:
		if !ignoreNullable && decl.IsNullable() {
			return fmt.Sprintf("fidl::tracking_ptr<%s>", declName(decl))
		}
		return declName(decl)
	case gidlmixer.NamedDeclaration:
		return declName(decl)
	case *gidlmixer.ArrayDecl:
		return fmt.Sprintf("fidl::Array<%s, %d>", typeName(decl.Elem()), decl.Size())
	case *gidlmixer.VectorDecl:
		return fmt.Sprintf("fidl::VectorView<%s>", typeName(decl.Elem()))
	case *gidlmixer.HandleDecl:
		switch decl.Subtype() {
		case fidl.Handle:
			return "zx::handle"
		case fidl.Channel:
			return "zx::channel"
		case fidl.Event:
			return "zx::event"
		default:
			panic(fmt.Sprintf("Handle subtype not supported %s", decl.Subtype()))
		}
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
	parts := strings.Split(decl.Name(), "/")
	parts = append([]string{"llcpp"}, parts...)
	return strings.Join(parts, "::")
}
