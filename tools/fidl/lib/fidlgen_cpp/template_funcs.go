// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"bytes"
	"fmt"
	"reflect"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Helper functions used by templates.

// ensureNamespace changes the current namespace to the one supplied and
// returns the C++ code required to switch to that namespace.
func ensureNamespace(arg interface{}) string {
	lines := []string{}

	newNamespace := []string{}
	switch v := arg.(type) {
	case namespaced:
		newNamespace = []string(v.Namespace())
	case string:
		newNamespace = strings.Split(v, "::")
		for len(newNamespace) > 0 && newNamespace[0] == "" {
			newNamespace = newNamespace[1:]
		}
	default:
		panic(fmt.Sprintf("Unexpected %T argument to EnsureNamespace", arg))
	}

	// Copy the namespaces
	new := make([]string, len(newNamespace))
	copy(new, newNamespace)
	current := make([]string, len(currentNamespace))
	copy(current, currentNamespace)

	// Remove common prefix
	for len(new) > 0 && len(current) > 0 && new[0] == current[0] {
		new = new[1:]
		current = current[1:]
	}

	// Leave the current namespace
	for i := len(current) - 1; i >= 0; i-- {
		lines = append(lines, fmt.Sprintf("}  // namespace %s", current[i]))
	}

	// Enter thew new namespace
	for i := 0; i < len(new); i++ {
		lines = append(lines, fmt.Sprintf("namespace %s {", new[i]))
	}

	// Update the current namespace variable
	currentNamespace = namespace(newNamespace)

	return strings.Join(lines, "\n")
}

// During template processing this holds the stack of namespaces.
// When a template calls IfdefFuchsia the current namespace is pushed onto the
// stack. When a template calls EndifFuchsia a namespace is popped off the
// stack and C++ code needed to go from the current namespace to the popped
// namespace is generated.
// This allows templates to maintain a consistent C++ namespace as they enter
// and leave #ifdef __Fuchsia__ blocks.
var namespaceStack = []namespace{}

// During template processing this holds the current namespace.
var currentNamespace namespace

func ifdefFuchsia() string {
	namespaceStack = append(namespaceStack, currentNamespace)

	if len(namespaceStack) == 1 {
		return "\n#ifdef __Fuchsia__\n"
	}
	return ""
}

func endifFuchsia() string {
	last := len(namespaceStack) - 1
	ns := namespaceStack[last]
	namespaceStack = namespaceStack[:last]
	s := ensureNamespace(ns)
	if len(namespaceStack) == 0 {
		return s + "\n#endif  // __Fuchsia__\n"
	}
	return s
}

// A per-file set of coding table definitions that have been imported via `__LOCAL extern "C"`
// declarations, used to prevent duplicate imports.
var externedCodingTables = map[string]struct{}{}

func endOfFile() string {
	externedCodingTables = map[string]struct{}{}
	if len(namespaceStack) != 0 {
		panic("The namespace stack isn't empty, there's a EndifFuchsia missing somewhere")
	}
	return ensureNamespace("::")
}

// ensureCodingTableDecl determines whether or not a particular name has already been imported into
// a `*.h` or `*.cc` file via a `__LOCAL extern "C"`-style declaration, thereby preventing duplicate
// instances of this declaration.
func ensureCodingTableDecl(codingTable *name) string {
	if codingTable == nil {
		return ""
	}

	n := codingTable.Name()
	if _, found := externedCodingTables[n]; found {
		return ""
	}

	externedCodingTables[n] = struct{}{}
	return ensureNamespace(codingTable) + "__LOCAL extern \"C\" const fidl_type_t " + codingTable.Name() + ";"
}

var currentTransport *Transport

func setTransport(name string) string {
	if transport, ok := transports[name]; ok {
		currentTransport = transport
		return ""
	}
	panic(fmt.Sprintf("unknown transport %s", name))
}

func unsetTransport() string {
	if currentTransport == nil {
		panic("cannot unset already unset transport")
	}
	currentTransport = nil
	return ""
}

// formatParam funcs are helpers that transform a type and name into a string
// for rendering in a template.
type formatParam func(string, Type) string

// visitSliceMembers visits each member of nested slices passed in and calls
// |fn| on each of them in depth first order.
func visitSliceMembers(val reflect.Value, fn func(interface{})) {
	switch val.Type().Kind() {
	case reflect.Slice:
		for j := 0; j < val.Len(); j++ {
			visitSliceMembers(val.Index(j), fn)
		}
	case reflect.Interface:
		visitSliceMembers(val.Elem(), fn)
	default:
		fn(val.Interface())
	}
}

func wireParam(n string, t Type) string {
	if t.Kind == TypeKinds.Array || t.Kind == TypeKinds.Struct {
		if !t.Nullable {
			if t.IsResource {
				return fmt.Sprintf("%s&& %s", t.String(), n)
			}
			return fmt.Sprintf("const %s& %s", t.String(), n)
		}
	}
	if t.Kind == TypeKinds.Handle || t.Kind == TypeKinds.Request || t.Kind == TypeKinds.Protocol {
		return fmt.Sprintf("%s&& %s", t.String(), n)
	}
	return fmt.Sprintf("%s %s", t.String(), n)
}

func unifiedParam(n string, t Type) string {
	if t.IsResource {
		return fmt.Sprintf("%s&& %s", t.String(), n)
	}
	return fmt.Sprintf("%s %s", t.String(), n)
}

func param(n string, t Type) string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("called param(%s, %v) when currentVariant isn't set.\n",
			n, t)
	case hlcppVariant:
		fidlgen.TemplateFatalf("HLCPP is not supported")
	case unifiedVariant:
		return unifiedParam(n, t)
	case wireVariant:
		return wireParam(n, t)
	}
	panic("not reached")
}

func wireForwardParam(n string, t Type) string {
	if t.Kind == TypeKinds.Union && t.IsResource {
		return fmt.Sprintf("std::move(%s)", n)
	} else if t.Kind == TypeKinds.Array || t.Kind == TypeKinds.Struct {
		if t.IsResource && !t.Nullable {
			return fmt.Sprintf("std::move(%s)", n)
		}
	} else if t.Kind == TypeKinds.Handle || t.Kind == TypeKinds.Request || t.Kind == TypeKinds.Protocol {
		return fmt.Sprintf("std::move(%s)", n)
	}
	return n
}

func unifiedForwardParam(n string, t Type) string {
	if t.Kind == TypeKinds.Bits || t.Kind == TypeKinds.Enum || t.Kind == TypeKinds.Primitive {
		return n
	}
	return fmt.Sprintf("std::move(%s)", n)
}

func forwardParam(n string, t Type) string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("called forwardParam(%s, %v) when currentVariant isn't set.\n",
			n, t)
	case hlcppVariant:
		fidlgen.TemplateFatalf("HLCPP is not supported")
	case unifiedVariant:
		return unifiedForwardParam(n, t)
	case wireVariant:
		return wireForwardParam(n, t)
	}
	panic("not reached")
}

// renderParams renders a nested list of parameter definitions.
// The parameter definitions are either strings or Members.
// Parameter structs are rendered with the supplied format func.
// The strings and formatted Parameters are joined with commas and returned.
func renderParams(format formatParam, list interface{}) string {
	var (
		buf   bytes.Buffer
		first = true
	)
	visitSliceMembers(reflect.ValueOf(list), func(val interface{}) {
		if val == nil {
			panic(fmt.Sprintf("Unexpected nil in %#v", list))
		}
		if first {
			first = false
		} else {
			buf.WriteString(", ")
		}
		switch val := val.(type) {
		case string:
			buf.WriteString(val)
		case Member:
			n, t := val.NameAndType()
			buf.WriteString(format(n, t))
		default:
			panic(fmt.Sprintf("Invalid RenderParams arg %#v", val))
		}
	})

	return buf.String()
}

// CommonTemplateFuncs holds a template.FuncMap containing common funcs.
var commonTemplateFuncs = template.FuncMap{
	"Eq":  func(a interface{}, b interface{}) bool { return a == b },
	"NEq": func(a interface{}, b interface{}) bool { return a != b },
	"Add": func(a int, b int) int { return a + b },

	"Kinds":       func() interface{} { return Kinds },
	"FamilyKinds": func() interface{} { return FamilyKinds },
	"TypeKinds":   func() interface{} { return TypeKinds },

	"IfdefFuchsia":          ifdefFuchsia,
	"EndifFuchsia":          endifFuchsia,
	"EnsureNamespace":       ensureNamespace,
	"EnsureCodingTableDecl": ensureCodingTableDecl,
	"EndOfFile":             endOfFile,

	"SetTransport":   setTransport,
	"UnsetTransport": unsetTransport,

	// UseHLCPP sets the template engine to default to the "hlcpp" domain object
	// namespace, when printing nameVariants.
	//
	// Example of HLCPP type name: "fuchsia::library::MyType".
	"UseHLCPP": func() string {
		currentVariant = hlcppVariant
		return ""
	},

	// UseUnified sets the template engine to default to the "unified" domain object
	// namespace, when printing nameVariants.
	//
	// Example of Unified type name: "fuchsia_library::MyType".
	"UseUnified": func() string {
		currentVariant = unifiedVariant
		return ""
	},

	// UseWire sets the template engine to default to the "wire" domain object
	// namespace, when printing nameVariants.
	//
	// Example of Wire type name: "fuchsia_library::wire::MyType".
	"UseWire": func() string {
		currentVariant = wireVariant
		return ""
	},

	"SkipRequestResponseDecls": func(decls []Kinded) []Kinded {
		var filtered []Kinded
		for _, decl := range decls {
			if s, ok := decl.(*Struct); ok {
				if s.IsAnonymousRequestOrResponse() {
					continue
				}
			}
			filtered = append(filtered, decl)
		}
		return filtered
	},

	// Renders a list of parameters in a declaration, without extra decoration.
	//
	// Usage:
	//   (RenderParams .Args "Foo local_var")
	//
	// Output:
	//   Arg1 arg1, Foo local_var
	//
	"RenderParams": func(params ...interface{}) string {
		return renderParams(param, params)
	},

	// Renders a list of parameters, but wraps every name in `std::move`.
	// This is useful for method invocations.
	//
	// Usage:
	//   (RenderForwardParams .Args "foo")
	//
	// Output:
	//   std::move(arg1), std::move(foo)
	//
	"RenderForwardParams": func(params ...interface{}) string {
		return renderParams(forwardParam, params)
	},

	"RenderInitMessage": func(params ...interface{}) string {
		s := renderParams(func(n string, t Type) string {
			return n + "(" + forwardParam(n, t) + ")"
		}, params)
		if len(s) == 0 {
			return ""
		}
		return ": " + s
	},

	// List is a helper to return a list of its arguments.
	"List": func(items ...interface{}) []interface{} {
		return items
	},
}

func mergeFuncMaps(all ...template.FuncMap) template.FuncMap {
	merged := template.FuncMap{}
	for _, funcs := range all {
		for k, fn := range funcs {
			merged[k] = fn
		}
	}
	return merged
}
