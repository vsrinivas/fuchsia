// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

const transportNamespaceMarker = "[TRANSPORT]"

var zxNs namespace = newNamespace("zx")
var fidlNs namespace = newNamespace("fidl")
var internalNs namespace = fidlNs.append("internal")
var testingNs namespace = fidlNs.append("testing")
var transportNs namespace = newNamespace(transportNamespaceMarker)

// variant controls how we refer to domain object declarations.
type variant string

const (
	noVariant      variant = ""
	hlcppVariant   variant = "hlcpp"
	unifiedVariant variant = "unified"
	wireVariant    variant = "wire"

	// for use in testing:
	testingVariant variant = "testing"
)

var currentVariant = noVariant

// Namespace represents a C++ namespace.
type namespace []string

func newNamespace(ns string) namespace {
	return namespace(strings.Split(ns, "::"))
}

// Namespace is implemented to satisfy the namespaced interface.
func (ns namespace) Namespace() namespace {
	return ns
}

// String returns the fully qualified namespace including leading ::.
func (ns namespace) String() string {
	if len(ns) == 0 {
		return ""
	}
	return "::" + ns.NoLeading()
}

// NoLeading returns the fully qualified namespace without the leading ::.
func (ns namespace) NoLeading() string {
	s := strings.Join(ns, "::")
	if currentTransport != nil {
		s = strings.Replace(s, transportNamespaceMarker, currentTransport.Namespace, 1)
	}
	return s
}

// append returns a new namespace with an additional component.
func (ns namespace) append(part string) namespace {
	newNs := make([]string, len(ns)+1)
	copy(newNs, ns)
	newNs[len(ns)] = part
	return namespace(newNs)
}

// member creates a named declaration within the namespace
func (ns namespace) member(n string) name {
	return name{name: stringNamePart(n), ns: ns}
}

// nameVariants is the name of a type or a template used in the various C++ bindings.
//
// Names are more general than FIDL declarations. All declarations have a corresponding
// type name, but some types are not declared in the generated code (e.g. zx::vmo),
// or are non-nominal (e.g. std::vector<FooBarDecl>).
type nameVariants struct {
	HLCPP name

	// Unified is like HLCPP, except it consists of type aliases, declared in
	// the unified bindings, to natural types. For example, the HLCPP name
	//
	//     fuchsia::my::lib::FooStruct
	//
	// would be aliased to the Unified name
	//
	//     fuchsia_my_lib::FooStruct.
	//
	// Similarly, if HLCPP is
	//
	//     std::array<std::unique_ptr<fuchsia::my::lib::FooStruct>, 5>
	//
	// then Unified would use the alias in the template argument:
	//
	//     std::array<std::unique_ptr<fuchsia_my_lib::FooStruct>, 5>.
	//
	// In case of client and server protocol endpoints, there is no alias,
	// and Unified is the same as HLCPP.
	Unified name

	Wire name
}

// commonNameVariants returns a nameVariants with the same Name for both Wire and HLCPP variants.
func commonNameVariants(decl name) nameVariants {
	return nameVariants{
		HLCPP:   decl,
		Unified: decl,
		Wire:    decl,
	}
}

func (dn nameVariants) String() string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called nameVariants.String() on %s/%s when currentVariant isn't set.\n",
			dn.HLCPP, dn.Wire)
	case hlcppVariant:
		return dn.HLCPP.String()
	case unifiedVariant:
		return dn.Unified.String()
	case wireVariant:
		return dn.Wire.String()
	case testingVariant:
		return fmt.Sprintf("%#v", dn)
	}
	panic("not reached")
}

func (dn nameVariants) Name() string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called nameVariants.Name() on %s/%s when currentVariant isn't set.\n",
			dn.HLCPP, dn.Wire)
	case hlcppVariant:
		return dn.HLCPP.Name()
	case unifiedVariant:
		return dn.Unified.Name()
	case wireVariant:
		return dn.Wire.Name()
	}
	panic("not reached")
}

func (dn nameVariants) Self() string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called nameVariants.Self() on %s/%s when currentVariant isn't set.\n", dn.HLCPP, dn.Wire)
	case hlcppVariant:
		return dn.HLCPP.Self()
	case unifiedVariant:
		return dn.Unified.Self()
	case wireVariant:
		return dn.Wire.Self()
	}
	panic("not reached")
}

func (dn nameVariants) NoLeading() string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called nameVariants.NoLeading() on %s/%s when currentVariant isn't set.\n", dn.HLCPP, dn.Wire)
	case hlcppVariant:
		return dn.HLCPP.NoLeading()
	case unifiedVariant:
		return dn.Unified.NoLeading()
	case wireVariant:
		return dn.Wire.NoLeading()
	}
	panic("not reached")
}

func (dn nameVariants) Namespace() namespace {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called nameVariants.Namespace() on %s/%s when currentVariant isn't set.\n",
			dn.HLCPP, dn.Wire)
	case hlcppVariant:
		return dn.HLCPP.Namespace()
	case unifiedVariant:
		return dn.Unified.Namespace()
	case wireVariant:
		return dn.Wire.Namespace()
	}
	panic("not reached")
}

// appendName returns a new nameVariants with an suffix appended to the name portions.
func (dn nameVariants) appendName(suffix string) nameVariants {
	return nameVariants{
		HLCPP:   dn.HLCPP.appendName(suffix),
		Unified: dn.Unified.appendName(suffix),
		Wire:    dn.Wire.appendName(suffix),
	}
}

// prependName returns a new nameVariants with an prefix prepended to the name portions.
func (dn nameVariants) prependName(prefix string) nameVariants {
	return nameVariants{
		HLCPP:   dn.HLCPP.prependName(prefix),
		Unified: dn.Unified.prependName(prefix),
		Wire:    dn.Wire.prependName(prefix),
	}
}

// appendNamespace returns a new nameVariants with additional C++ namespace components appended.
func (dn nameVariants) appendNamespace(c string) nameVariants {
	return nameVariants{
		HLCPP:   dn.HLCPP.appendNamespace(c),
		Unified: dn.Unified.appendNamespace(c),
		Wire:    dn.Wire.appendNamespace(c),
	}
}

// nest returns a new name for a class nested inside the existing name.
func (dn nameVariants) nest(c string) nameVariants {
	return nameVariants{
		HLCPP:   dn.HLCPP.nest(c),
		Unified: dn.Unified.nest(c),
		Wire:    dn.Wire.nest(c),
	}
}

// nestVariants returns a new name for a class nested inside the existing name.
func (dn nameVariants) nestVariants(v nameVariants) nameVariants {
	if len(v.HLCPP.Namespace()) != 0 {
		panic(fmt.Sprintf("Can't nest a name with a namespace: %v", v.HLCPP.String()))
	}
	if len(v.Unified.Namespace()) != 0 {
		panic(fmt.Sprintf("Can't nest a name with a namespace: %v", v.Unified.String()))
	}
	if len(v.Wire.Namespace()) != 0 {
		panic(fmt.Sprintf("Can't nest a name with a namespace: %v", v.Wire.String()))
	}
	return nameVariants{
		HLCPP:   dn.HLCPP.nest(v.HLCPP.Name()),
		Unified: dn.Unified.nest(v.Unified.Name()),
		Wire:    dn.Wire.nest(v.Wire.Name()),
	}
}

// nameVariantsForHandle returns the C++ name for a handle type
func nameVariantsForHandle(t fidlgen.HandleSubtype) nameVariants {
	if typeName, ok := handleTypeNames[t]; ok {
		return commonNameVariants(zxNs.member(typeName))
	}
	return commonNameVariants(zxNs.member(string(t)))
}

// Type names for to use for handles where the name isn't the same as HandleSubtype.
// For any subtype not in this list, string(HandleSubtype) is used instead.
var handleTypeNames = map[fidlgen.HandleSubtype]string{
	fidlgen.SuspendToken: "suspend_token",
}

// primitiveNameVariants returns a nameVariants for a primitive type, common across all bindings.
func primitiveNameVariants(primitive string) nameVariants {
	return commonNameVariants(makeName(primitive))
}

// namePart represents part of non-namespace part of a name.
// It's implemented by three types: stringNamePart, nestedNamePart and templateNamePart.
// These form a tree to hold the structure of a name so that it can be accessed and manipulated safely.
// For example in fidl::WireServer<fuchsia_library::Protocol>::ProtocolMethod this would be:
//   WireServer<fuchsia_library::Protocol>::ProtocolMethod
//   |-nestedNP---------------------------------------------|
//   |-templateNP---------------------------|  |-stringNP---|
//   |-stringNP---|-Name--------------------|
// TODO(ianloic): rename to idPart
type namePart interface {
	// String returns the full name.
	String() string
	// Self returns how the type refers to itself, like in constructor & destructor names.
	// For a nested name like "Foo::Bar::Baz" this would be "Baz".
	// For a template name like "Foo::Bar<Baz>" this would be "Bar".
	Self() string

	// nest returns a new name for a class nested inside the existing name.
	nest(name string) namePart
	// Template returns a new name with this name being an template applied to the |args|.
	template(args string) namePart
	// prependName returns a new name with a prefix prepended.
	prependName(prefix string) namePart
	// appendName returns a new name with a suffix appended.
	appendName(suffix string) namePart
}

type stringNamePart string

var _ namePart = (*stringNamePart)(nil)

func (n stringNamePart) String() string {
	return string(n)
}

func (n stringNamePart) Self() string {
	return string(n)
}

func (n stringNamePart) nest(name string) namePart {
	return newNestedNamePart(n, stringNamePart(name))
}

func (n stringNamePart) template(args string) namePart {
	return newTemplateNamePart(n, args)
}

func (n stringNamePart) prependName(prefix string) namePart {
	return stringNamePart(prefix + string(n))
}

func (n stringNamePart) appendName(suffix string) namePart {
	return stringNamePart(string(n) + suffix)
}

type nestedNamePart struct {
	left  namePart
	right namePart
}

var _ namePart = (*nestedNamePart)(nil)

func newNestedNamePart(left, right namePart) namePart {
	return nestedNamePart{left, right}
}

func (n nestedNamePart) String() string {
	return fmt.Sprintf("%s::%s", n.left, n.right)
}

func (n nestedNamePart) Self() string {
	return n.right.Self()
}

func (n nestedNamePart) nest(name string) namePart {
	return nestedNamePart{n.left, n.right.nest(name)}
}

func (n nestedNamePart) template(args string) namePart {
	return newTemplateNamePart(n, args)
}

func (n nestedNamePart) prependName(prefix string) namePart {
	return nestedNamePart{n.left, n.right.prependName(prefix)}
}

func (n nestedNamePart) appendName(suffix string) namePart {
	return nestedNamePart{n.left, n.right.appendName(suffix)}
}

type templateNamePart struct {
	tmpl namePart
	args string
}

var _ namePart = (*templateNamePart)(nil)

func newTemplateNamePart(tmpl namePart, args string) namePart {
	return templateNamePart{tmpl, args}
}

func (n templateNamePart) String() string {
	return fmt.Sprintf("%s<%s>", n.tmpl, n.args)
}

func (n templateNamePart) Self() string {
	return n.tmpl.Self()
}

func (n templateNamePart) nest(name string) namePart {
	return nestedNamePart{n, stringNamePart(name)}
}

func (n templateNamePart) template(args string) namePart {
	panic(fmt.Sprintf("Can't make a template of a template: %s", n))
}

func (n templateNamePart) prependName(prefix string) namePart {
	panic(fmt.Sprintf("Can't prepend to the name of a template: %s", n))
}

func (n templateNamePart) appendName(suffix string) namePart {
	panic(fmt.Sprintf("Can't append to the name of a template: %s", n))
}

// name holds a C++ qualified identifier.
// See: https://en.cppreference.com/w/cpp/language/identifiers#Qualified_identifiers
// It consists of a Namespace and a namePart.
// TODO(ianloic): move this to the top of the file since it's the most important type.
type name struct {
	name namePart
	ns   namespace
}

// makeName takes a string with a :: separated name and makes a Name treating the last component
// as the local name and the preceding components as the namespace.
// This should only be used with string literals for creating well-known, simple names.
func makeName(n string) name {
	i := strings.LastIndex(n, "::")
	if i == -1 {
		return name{name: stringNamePart(n)}
	}
	if i == 0 {
		panic(fmt.Sprintf("Don't call MakeName with leading double-colons: %v", n))
	}
	return name{
		name: stringNamePart(n[i+2:]),
		ns:   newNamespace(n[0:i]),
	}
}

// simpleName return a name with a single component
func simpleName(n string) name {
	if strings.ContainsAny(n, ":-./") {
		panic(fmt.Sprintf("%#v is not a simple name", n))
	}
	return name{name: stringNamePart(n)}
}

// String returns the full name with a leading :: if the name has a namespace.
func (n name) String() string {
	ns := n.ns.String()
	if len(ns) > 0 {
		ns = ns + "::"
	}
	return ns + n.name.String()
}

// Name returns the portion of the name that comes after the namespace.
func (n name) Name() string {
	return n.name.String()
}

// Self returns how the type refers to itself, like in constructor & destructor names.
// For a nested name like "Foo::Bar::Baz" this would be "Baz".
// For a template name like "Foo::Bar<Baz>" this would be "Bar".
func (n name) Self() string {
	return n.name.Self()
}

// TODO(ianloic): probably make this the default
func (n name) NoLeading() string {
	ns := n.ns.NoLeading()
	if len(ns) > 0 {
		ns = ns + "::"
	}
	return ns + n.name.String()
}

// namespace returns the namespace portion of the name.
func (n name) Namespace() namespace {
	return n.ns
}

// nest returns a new name for a class nested inside the existing name.
func (n name) nest(nested string) name {
	return name{name: n.name.nest(nested), ns: n.ns}
}

// Template returns a new name with this name being an template applied to the |arg|.
func (n name) template(arg name) name {
	return name{name: n.name.template(arg.String()), ns: n.ns}
}

// arrayTemplate returns a new name with this name being an template applied to the |arg| with a |count|.
func (n name) arrayTemplate(arg name, count int) name {
	return name{name: n.name.template(fmt.Sprintf("%s, %d", arg.String(), count)), ns: n.ns}
}

// prependName returns a new name with a prefix prepended to the last part of the name.
func (n name) prependName(prefix string) name {
	return name{name: n.name.prependName(prefix), ns: n.ns}
}

// appendName returns a new name with a suffix appended to the last part of the name.
func (n name) appendName(suffix string) name {
	return name{name: n.name.appendName(suffix), ns: n.ns}
}

// appendNamespace returns a new name with an additional namespace component added.
func (n name) appendNamespace(part string) name {
	return name{name: n.name, ns: n.ns.append(part)}
}

// makeTupleName returns a Name for a std::tuple of the supplied names.
func makeTupleName(members []name) name {
	t := makeName("std::tuple")
	a := []string{}
	for _, m := range members {
		a = append(a, m.String())
	}
	return name{name: t.name.template(strings.Join(a, ", ")), ns: t.ns}
}
