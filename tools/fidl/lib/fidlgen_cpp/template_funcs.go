// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"
	"text/template"
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

func endOfFile() string {
	if len(namespaceStack) != 0 {
		panic("The namespace stack isn't empty, there's a EndifFuchsia missing somewhere")
	}
	return ensureNamespace("::")
}

// CommonTemplateFuncs holds a template.FuncMap containing common funcs.
var commonTemplateFuncs = template.FuncMap{
	"Eq":  func(a interface{}, b interface{}) bool { return a == b },
	"NEq": func(a interface{}, b interface{}) bool { return a != b },

	"Kinds":       func() interface{} { return Kinds },
	"FamilyKinds": func() interface{} { return FamilyKinds },
	"TypeKinds":   func() interface{} { return TypeKinds },

	"IfdefFuchsia":    ifdefFuchsia,
	"EndifFuchsia":    endifFuchsia,
	"EnsureNamespace": ensureNamespace,
	"EndOfFile":       endOfFile,

	// UseNatural sets the template engine to default to the "natural" domain object
	// namespace, when printing nameVariants.
	//
	// Example of Natural type name: "fuchsia::library::MyType".
	"UseNatural": func() string {
		currentVariant = naturalVariant
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
