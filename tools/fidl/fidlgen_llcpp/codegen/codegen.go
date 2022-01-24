// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"embed"
	"fmt"
	"text/template"

	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

type Generator struct {
	*cpp.Generator
}

type TypedArgument struct {
	ArgumentName  string
	ArgumentValue string
	ArgumentType  cpp.Type
	Pointer       bool
	Nullable      bool
	Access        bool
	MutableAccess bool
}

func closeHandles(argumentName string, argumentValue string, argumentType cpp.Type, pointer bool, nullable bool, access bool, mutableAccess bool) string {
	if !argumentType.IsResource {
		return ""
	}
	name := argumentName
	value := argumentValue
	if access || mutableAccess {
		name = fmt.Sprintf("%s()", name)
		value = name
	}

	switch argumentType.Kind {
	case cpp.TypeKinds.Handle, cpp.TypeKinds.Request, cpp.TypeKinds.Protocol:
		if pointer {
			if nullable {
				return fmt.Sprintf("if (%s != nullptr) { %s->reset(); }", name, name)
			}
			return fmt.Sprintf("%s->reset();", name)
		} else {
			return fmt.Sprintf("%s.reset();", name)
		}
	case cpp.TypeKinds.Array:
		element_name := argumentName + "_element"
		element_type := argumentType.ElementType
		var buf bytes.Buffer
		buf.WriteString("{\n")
		buf.WriteString(fmt.Sprintf("%s* %s = %s.data();\n", element_type, element_name, value))
		buf.WriteString(fmt.Sprintf("for (size_t i = 0; i < %s.size(); ++i, ++%s) {\n", value, element_name))
		buf.WriteString(closeHandles(element_name, fmt.Sprintf("(*%s)", element_name), *element_type, true, false, false, false))
		buf.WriteString("\n}\n}\n")
		return buf.String()
	case cpp.TypeKinds.Vector:
		element_name := argumentName + "_element"
		element_type := argumentType.ElementType
		var buf bytes.Buffer
		buf.WriteString("{\n")
		buf.WriteString(fmt.Sprintf("%s* %s = %s.mutable_data();\n", element_type, element_name, value))
		buf.WriteString(fmt.Sprintf("for (uint64_t i = 0; i < %s.count(); ++i, ++%s) {\n", value, element_name))
		buf.WriteString(closeHandles(element_name, fmt.Sprintf("(*%s)", element_name), *element_type, true, false, false, false))
		buf.WriteString("\n}\n}\n")
		return buf.String()
	default:
		if pointer {
			if nullable {
				return fmt.Sprintf("if (%s != nullptr) { %s->_CloseHandles(); }", name, name)
			}
			return fmt.Sprintf("%s->_CloseHandles();", name)
		} else {
			return fmt.Sprintf("%s._CloseHandles();", name)
		}
	}
}

// These are the helper functions we inject for use by the templates.
var utilityFuncs = template.FuncMap{
	"SyncCallTotalStackSizeV1": func(m cpp.Method) int {
		totalSize := 0
		if m.Request.ClientAllocationV1.IsStack {
			totalSize += m.Request.ClientAllocationV1.Size
		}
		if m.Response.ClientAllocationV1.IsStack {
			totalSize += m.Response.ClientAllocationV1.Size
		}
		return totalSize
	},
	"SyncCallTotalStackSizeV2": func(m cpp.Method) int {
		totalSize := 0
		if m.Request.ClientAllocationV2.IsStack {
			totalSize += m.Request.ClientAllocationV2.Size
		}
		if m.Response.ClientAllocationV2.IsStack {
			totalSize += m.Response.ClientAllocationV2.Size
		}
		return totalSize
	},
	"CloseHandles": func(member cpp.Member,
		access bool,
		mutableAccess bool) string {
		n, t := member.NameAndType()
		return closeHandles(n, n, t, t.WirePointer, t.WirePointer, access, mutableAccess)
	},
}

//go:embed *.tmpl driver/*.tmpl
var templates embed.FS

func NewGenerator(flags *cpp.CmdlineFlags) *cpp.Generator {
	return cpp.NewGenerator(flags, templates, utilityFuncs)
}
