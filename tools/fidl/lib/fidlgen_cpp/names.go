// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var zxNs Namespace = NewNamespace("zx")
var fidlNs Namespace = NewNamespace("fidl")
var internalNs Namespace = fidlNs.Append("internal")

// variant controls how we refer to domain object declarations.
type variant string

const (
	noVariant      variant = ""
	naturalVariant variant = "natural"
	unifiedVariant variant = "unified"
	wireVariant    variant = "wire"
)

var currentVariant = noVariant

// UseNatural sets the template engine to default to the "natural" domain object
// namespace, when printing NameVariants.
//
// Example of Natural type name: "fuchsia::library::MyType".
func UseNatural() string {
	currentVariant = naturalVariant
	return ""
}

// UseUnified sets the template engine to default to the "unified" domain object
// namespace, when printing NameVariants.
//
// Example of Unified type name: "fuchsia_library::MyType".
func UseUnified() string {
	currentVariant = unifiedVariant
	return ""
}

// UseWire sets the template engine to default to the "wire" domain object
// namespace, when printing NameVariants.
//
// Example of Wire type name: "fuchsia_library::wire::MyType".
func UseWire() string {
	currentVariant = wireVariant
	return ""
}

// Namespace represents a C++ namespace.
type Namespace []string

func NewNamespace(ns string) Namespace {
	return Namespace(strings.Split(ns, "::"))
}

// Namespace is implemented to satisfy the Namespaced interface.
func (ns Namespace) Namespace() Namespace {
	return ns
}

// String returns the fully qualified namespace including leading ::.
func (ns Namespace) String() string {
	if len(ns) == 0 {
		return ""
	}
	return "::" + ns.NoLeading()
}

// NoLeading returns the fully qualified namespace without the leading ::.
func (ns Namespace) NoLeading() string {
	return strings.Join(ns, "::")
}

// Append returns a new namespace with an additional component.
func (ns Namespace) Append(part string) Namespace {
	newNs := make([]string, len(ns)+1)
	copy(newNs, ns)
	newNs[len(ns)] = part
	return Namespace(newNs)
}

// DropLastComponent returns a new namespace with the final component removed.
func (ns Namespace) DropLastComponent() Namespace {
	if len(ns) == 0 {
		panic("Can't drop the end of an empty namespace")
	}
	new := make([]string, len(ns)-1)
	copy(new, ns)
	return Namespace(new)
}

// Member creates a named declaration within the namespace
func (ns Namespace) Member(name string) Name {
	return Name{name: stringNamePart(name), ns: ns}
}

// NameVariants is the name of a type or a template used in the various C++ bindings.
//
// Names are more general than FIDL declarations. All declarations have a corresponding
// type name, but some types are not declared in the generated code (e.g. zx::vmo),
// or are non-nominal (e.g. std::vector<FooBarDecl>).
type NameVariants struct {
	Natural Name

	// Unified is like Natural, except it consists of type aliases, declared in
	// the unified bindings, to natural types. For example, the Natural name
	//
	//     fuchsia::my::lib::FooStruct
	//
	// would be aliased to the Unified name
	//
	//     fuchsia_my_lib::FooStruct.
	//
	// Similarly, if Natural is
	//
	//     std::array<std::unique_ptr<fuchsia::my::lib::FooStruct>, 5>
	//
	// then Unified would use the alias in the template argument:
	//
	//     std::array<std::unique_ptr<fuchsia_my_lib::FooStruct>, 5>.
	//
	// In case of client and server protocol endpoints, there is no alias,
	// and Unified is the same as Natural.
	Unified Name

	Wire Name
}

// CommonNameVariants returns a NameVariants with the same Name for both Wire and Natural variants.
func CommonNameVariants(decl Name) NameVariants {
	return NameVariants{
		Natural: decl,
		Unified: decl,
		Wire:    decl,
	}
}

func (dn NameVariants) String() string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called NameVariants.String() on %s/%s when currentVariant isn't set.\n",
			dn.Natural, dn.Wire)
	case naturalVariant:
		return dn.Natural.String()
	case unifiedVariant:
		return dn.Unified.String()
	case wireVariant:
		return dn.Wire.String()
	}
	panic("not reached")
}

func (dn NameVariants) Name() string {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called NameVariants.Name() on %s/%s when currentVariant isn't set.\n",
			dn.Natural, dn.Wire)
	case naturalVariant:
		return dn.Natural.Name()
	case unifiedVariant:
		return dn.Unified.Name()
	case wireVariant:
		return dn.Wire.Name()
	}
	panic("not reached")
}

func (dn NameVariants) Namespace() Namespace {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called NameVariants.Namespace() on %s/%s when currentVariant isn't set.\n",
			dn.Natural, dn.Wire)
	case naturalVariant:
		return dn.Natural.Namespace()
	case unifiedVariant:
		return dn.Unified.Namespace()
	case wireVariant:
		return dn.Wire.Namespace()
	}
	panic("not reached")
}

// AppendName returns a new NameVariants with an suffix appended to the name portions.
func (dn NameVariants) AppendName(suffix string) NameVariants {
	return NameVariants{
		Natural: dn.Natural.AppendName(suffix),
		Unified: dn.Unified.AppendName(suffix),
		Wire:    dn.Wire.AppendName(suffix),
	}
}

// PrependName returns a new NameVariants with an prefix prepended to the name portions.
func (dn NameVariants) PrependName(prefix string) NameVariants {
	return NameVariants{
		Natural: dn.Natural.PrependName(prefix),
		Unified: dn.Unified.PrependName(prefix),
		Wire:    dn.Wire.PrependName(prefix),
	}
}

// AppendNamespace returns a new NameVariants with additional C++ namespace components appended.
func (dn NameVariants) AppendNamespace(c string) NameVariants {
	return NameVariants{
		Natural: dn.Natural.AppendNamespace(c),
		Unified: dn.Unified.AppendNamespace(c),
		Wire:    dn.Wire.AppendNamespace(c),
	}
}

// NameVariantsForHandle returns the C++ name for a handle type
func NameVariantsForHandle(t fidlgen.HandleSubtype) NameVariants {
	return CommonNameVariants(zxNs.Member(string(t)))
}

// PrimitiveNameVariants returns a NameVariants for a primitive type, common across all bindings.
func PrimitiveNameVariants(primitive string) NameVariants {
	return CommonNameVariants(MakeName(primitive))
}

// namePart represents part of non-namespace part of a name.
// It's implemented by three types: stringNamePart, nestedNamePart and templateNamePart.
// These form a tree to hold the structure of a name so that it can be accessed and manipulated safely.
// For example in fidl::WireInterface<fuchsia_library::Protocol>::ProtocolMethod this would be:
//   WireInterface<fuchsia_library::Protocol>::ProtocolMethod
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

	// Nest returns a new name for a class nested inside the existing name.
	Nest(name string) namePart
	// Template returns a new name with this name being an template applied to the |args|.
	Template(args string) namePart
	// PrependName returns a new name with a prefix prepended.
	PrependName(prefix string) namePart
	// AppendName returns a new name with a suffix appended.
	AppendName(suffix string) namePart
}

type stringNamePart string

var _ namePart = (*stringNamePart)(nil)

func (n stringNamePart) String() string {
	return string(n)
}

func (n stringNamePart) Self() string {
	return string(n)
}

func (n stringNamePart) Nest(name string) namePart {
	return newNestedNamePart(n, stringNamePart(name))
}

func (n stringNamePart) Template(args string) namePart {
	return newTemplateNamePart(n, args)
}

func (n stringNamePart) PrependName(prefix string) namePart {
	return stringNamePart(prefix + string(n))
}

func (n stringNamePart) AppendName(suffix string) namePart {
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

func (n nestedNamePart) Nest(name string) namePart {
	return nestedNamePart{n.left, n.right.Nest(name)}
}

func (n nestedNamePart) Template(args string) namePart {
	return newTemplateNamePart(n, args)
}

func (n nestedNamePart) PrependName(prefix string) namePart {
	return nestedNamePart{n.left, n.right.PrependName(prefix)}
}

func (n nestedNamePart) AppendName(suffix string) namePart {
	return nestedNamePart{n.left, n.right.AppendName(suffix)}
}

type templateNamePart struct {
	template namePart
	args     string
}

var _ namePart = (*templateNamePart)(nil)

func newTemplateNamePart(template namePart, args string) namePart {
	return templateNamePart{template, args}
}

func (n templateNamePart) String() string {
	return fmt.Sprintf("%s<%s>", n.template, n.args)
}

func (n templateNamePart) Self() string {
	return n.template.Self()
}

func (n templateNamePart) Nest(name string) namePart {
	return nestedNamePart{n, stringNamePart(name)}
}

func (n templateNamePart) Template(args string) namePart {
	panic(fmt.Sprintf("Can't make a template of a template: %s", n))
}

func (n templateNamePart) PrependName(prefix string) namePart {
	panic(fmt.Sprintf("Can't prepend to the name of a template: %s", n))
}

func (n templateNamePart) AppendName(suffix string) namePart {
	panic(fmt.Sprintf("Can't append to the name of a template: %s", n))
}

// Name holds a C++ qualified identifier.
// See: https://en.cppreference.com/w/cpp/language/identifiers#Qualified_identifiers
// It consists of a Namespace and a namePart.
// TODO(ianloic): move this to the top of the file since it's the most important type.
type Name struct {
	name namePart
	ns   Namespace
}

// MakeName takes a string with a :: separated name and makes a Name treating the last component
// as the local name and the preceding components as the namespace.
// This should only be used with string literals for creating well-known, simple names.
func MakeName(name string) Name {
	i := strings.LastIndex(name, "::")
	if i == -1 {
		return Name{name: stringNamePart(name)}
	}
	if i == 0 {
		panic(fmt.Sprintf("Don't call MakeName with leading double-colons: %v", name))
	}
	return Name{
		name: stringNamePart(name[i+2:]),
		ns:   NewNamespace(name[0:i]),
	}
}

// String returns the full name with a leading :: if the name has a namespace.
func (n Name) String() string {
	ns := n.ns.String()
	if len(ns) > 0 {
		ns = ns + "::"
	}
	return ns + n.name.String()
}

// Name returns the portion of the name that comes after the namespace.
func (n Name) Name() string {
	return n.name.String()
}

// Self returns how the type refers to itself, like in constructor & destructor names.
// For a nested name like "Foo::Bar::Baz" this would be "Baz".
// For a template name like "Foo::Bar<Baz>" this would be "Bar".
func (n Name) Self() string {
	return n.name.Self()
}

// TODO(ianloic): probably make this the default
func (n Name) NoLeading() string {
	ns := n.ns.NoLeading()
	if len(ns) > 0 {
		ns = ns + "::"
	}
	return ns + n.name.String()
}

// Namespace returns the namespace portion of the name.
func (n Name) Namespace() Namespace {
	return n.ns
}

// Nest returns a new name for a class nested inside the existing name.
func (n Name) Nest(name string) Name {
	return Name{name: n.name.Nest(name), ns: n.ns}
}

// Template returns a new name with this name being an template applied to the |arg|.
func (n Name) Template(arg Name) Name {
	return Name{name: n.name.Template(arg.String()), ns: n.ns}
}

// ArrayTemplate returns a new name with this name being an template applied to the |arg| with a |count|.
func (n Name) ArrayTemplate(arg Name, count int) Name {
	return Name{name: n.name.Template(fmt.Sprintf("%s, %d", arg.String(), count)), ns: n.ns}
}

// PrependName returns a new name with a prefix prepended to the last part of the name.
func (n Name) PrependName(prefix string) Name {
	return Name{name: n.name.PrependName(prefix), ns: n.ns}
}

// AppendName returns a new name with a suffix appended to the last part of the name.
func (n Name) AppendName(suffix string) Name {
	return Name{name: n.name.AppendName(suffix), ns: n.ns}
}

// AppendNamespace returns a new name with an additional namespace component added.
func (n Name) AppendNamespace(part string) Name {
	return Name{name: n.name, ns: n.ns.Append(part)}
}

// MakeTupleName returns a Name for a std::tuple of the supplied names.
func MakeTupleName(members []Name) Name {
	t := MakeName("std::tuple")
	a := []string{}
	for _, m := range members {
		a = append(a, m.String())
	}
	return Name{name: t.name.Template(strings.Join(a, ", ")), ns: t.ns}
}
