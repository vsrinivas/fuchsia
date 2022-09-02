// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package zither contains abstractions and utilities shared by the various backends.
package zither

import (
	"fmt"
	"math/bits"
	"path/filepath"
	"reflect"
	"sort"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

// DeclOrder represents the ordering policy for declarations as they are
// provided to the backend.
type DeclOrder int

const (
	// SourceDeclOrder orders declarations as they were in source:
	// lexicographically by filename across FIDL files and then by location
	// within files.
	SourceDeclOrder DeclOrder = iota

	// DependencyDeclOrder gives a topological sorting of declarations by
	// dependency, so that a declaration is always preceded by the declarations
	// figuring into its definition. Dependency alone, however, gives a partial
	// order; this order is fixed in that it further orders declarations with
	// no interdependencies by source (as above).
	//
	// An order topologically sorted on dependency is a general requirement for
	// any backend generating C-family code. While technically speaking
	// forward-declarations could be used to rearrange the definitions of a
	// number of things, this is not an option with nested struct or union
	// definitions, as the nested layout types would need to be 'complete' in
	// that scope. Accordingly, it is simpler just to deal in full topological
	// sorts.
	DependencyDeclOrder
)

// FileSummary is a summarized representation of a FIDL source file.
type FileSummary struct {
	// Library is the associated FIDL library.
	Library fidlgen.LibraryName

	// Name is the extension-less basename of the file.
	Name string

	// Deps records the (extension-less) file names that this file depends on;
	// we say a file "depends" on another if the former has a declaration that
	// depends on a declaration in the latter.
	Deps map[string]struct{}

	// TypeKinds gives the kinds of types contained in this file's declarations.
	// This is useful for knowing the precise set of imports to make in the
	// code generated from this file.
	TypeKinds map[TypeKind]struct{}

	// The contained declarations.
	Decls []Decl
}

// Decl represents an abstract (summarized) FIDL declaration, meant for use in
// template logic and featuring thin wrappers around type assertions for
// deriving concrete types. In normal go code, we would do the type
// assertions directly, but no can do in templates.
type Decl struct {
	value interface{}
}

func (decl Decl) Name() fidlgen.Name {
	switch decl := decl.value.(type) {
	case *Const:
		return decl.Name
	case *Enum:
		return decl.Name
	case *Bits:
		return decl.Name
	case *Struct:
		return decl.Name
	default:
		panic(fmt.Sprintf("unknown declaration type: %s", reflect.TypeOf(decl).Name()))
	}
}

func (decl Decl) IsConst() bool {
	_, ok := decl.value.(*Const)
	return ok
}

func (decl Decl) AsConst() Const {
	return *decl.value.(*Const)
}

func (decl Decl) IsEnum() bool {
	_, ok := decl.value.(*Enum)
	return ok
}

func (decl Decl) AsEnum() Enum {
	return *decl.value.(*Enum)
}

func (decl Decl) IsBits() bool {
	_, ok := decl.value.(*Bits)
	return ok
}

func (decl Decl) AsBits() Bits {
	return *decl.value.(*Bits)
}

func (decl Decl) IsStruct() bool {
	_, ok := decl.value.(*Struct)
	return ok
}

func (decl Decl) AsStruct() Struct {
	return *decl.value.(*Struct)
}

type declMap map[string]fidlgen.Decl

// Summarize creates FIDL file summaries from FIDL IR. Within each file
// summary, declarations are ordered according to `order`.
func Summarize(ir fidlgen.Root, order DeclOrder) ([]FileSummary, error) {
	libName, err := fidlgen.ReadLibraryName(string(ir.Name))
	if err != nil {
		return nil, err
	}

	// We will process declarations in topological order (with respect to
	// dependency) and record declarations as we go; that way, we when we
	// process a particular declaration we will have full knowledge of is
	// dependencies, and by extension itself. This ordering exists just for
	// ease of processing and is independent of that prescribed by `order`.
	g := fidlgen_cpp.NewDeclDepGraph(ir)
	decls := g.SortedDecls()
	processed := make(declMap)

	filesByName := make(map[string]*FileSummary)
	getFile := func(decl fidlgen.Decl) *FileSummary {
		name := filepath.Base(decl.GetLocation().Filename)
		name = strings.TrimSuffix(name, ".test.fidl")
		name = strings.TrimSuffix(name, ".fidl")

		file, ok := filesByName[name]
		if !ok {
			file = &FileSummary{
				Library:   libName,
				Name:      name,
				Deps:      make(map[string]struct{}),
				TypeKinds: make(map[TypeKind]struct{}),
			}
			filesByName[name] = file
		}
		return file
	}

	for _, decl := range decls {
		typeKinds := make(map[TypeKind]struct{})
		var summarized interface{}
		var err error
		switch decl := decl.(type) {
		case *fidlgen.Const:
			summarized, err = newConst(*decl, processed)
			if err == nil {
				typeKinds[summarized.(*Const).Kind] = struct{}{}
			}
		case *fidlgen.Enum:
			summarized, err = newEnum(*decl)
			typeKinds[TypeKindInteger] = struct{}{}
		case *fidlgen.Bits:
			summarized, err = newBits(*decl)
			typeKinds[TypeKindInteger] = struct{}{}
		case *fidlgen.Struct:
			summarized, err = newStruct(*decl, processed, typeKinds)
		default:
			return nil, fmt.Errorf("unsupported declaration type: %s", fidlgen.GetDeclType(decl))
		}
		if err != nil {
			return nil, err
		}

		file := getFile(decl)
		d := Decl{summarized}
		file.Decls = append(file.Decls, d)
		for kind := range typeKinds {
			file.TypeKinds[kind] = struct{}{}
		}

		// Now go back and record the dependents' dependency on this declaration.
		dependents, ok := g.GetDirectDependents(decl.GetName())
		if !ok {
			panic(fmt.Sprintf("%s not found in declaration graph", decl.GetName()))
		}
		for _, dependent := range dependents {
			dependentFile := getFile(dependent)
			if dependentFile.Name != file.Name {
				dependentFile.Deps[file.Name] = struct{}{}
			}
		}
		processed[string(decl.GetName())] = decl
	}

	var files []FileSummary
	for _, file := range filesByName {
		// Now reorder declarations in the order expected by the backends.
		switch order {
		case SourceDeclOrder:
			sort.Slice(file.Decls, func(i, j int) bool {
				ith := processed[file.Decls[i].Name().String()]
				jth := processed[file.Decls[j].Name().String()]
				return fidlgen.LocationCmp(ith.GetLocation(), jth.GetLocation())
			})
		case DependencyDeclOrder:
			// Already in this order.
		default:
			panic(fmt.Sprintf("unknown declaration order: %v", order))
		}

		files = append(files, *file)
	}
	return files, nil
}

// TypeKind gives a rough categorization of FIDL primitive and declaration types.
type TypeKind string

const (
	TypeKindBool    TypeKind = "bool"
	TypeKindInteger TypeKind = "integer"
	TypeKindString  TypeKind = "string"
	TypeKindEnum    TypeKind = "enum"
	TypeKindBits    TypeKind = "bits"
	TypeKindArray   TypeKind = "array"
	TypeKindStruct  TypeKind = "struct"
)

// Const is a representation of a constant FIDL declaration.
type Const struct {
	// Name is the full name of the associated FIDL declaration.
	Name fidlgen.Name

	// Kind is the kind of the constant's type.
	Kind TypeKind

	// Type is the FIDL type of the constant. If Kind is TypeKindEnum or
	// TypeKindBits, then this field encodes a full declaration name of
	// the associated enum or bits.
	Type string

	// Value is the constant's value in string form.
	Value string

	// Identifier gives whether this constant was defined as another
	// constant, holding a pointer to that constant's name.
	Identifier *fidlgen.Name

	// Expression is the original FIDL expression given for the value,
	// included only when it meaningfully differs from the value.
	Expression string

	// Comments comprise the original docstring of the FIDL declaration.
	Comments []string
}

func newConst(c fidlgen.Const, decls declMap) (*Const, error) {
	var kind TypeKind
	var typ string
	switch c.Type.Kind {
	case fidlgen.PrimitiveType:
		if c.Type.PrimitiveSubtype.IsFloat() {
			return nil, fmt.Errorf("floats are unsupported")
		}
		typ = string(c.Type.PrimitiveSubtype)
		if typ == string(fidlgen.Bool) {
			kind = TypeKindBool
		} else {
			kind = TypeKindInteger
		}
	case fidlgen.StringType:
		typ = string(fidlgen.StringType)
		kind = TypeKindString
	case fidlgen.IdentifierType:
		typ = string(c.Type.Identifier)
		switch decls[typ].(type) {
		case *fidlgen.Enum:
			kind = TypeKindEnum
		case *fidlgen.Bits:
			kind = TypeKindBits
		default:
			return nil, fmt.Errorf("%v has unsupported constant type: %s", c.Name, fidlgen.GetDeclType(decls[typ]))
		}
	default:
		return nil, fmt.Errorf("%v has unsupported constant type: %s", c.Name, c.Type.Kind)
	}

	name, err := fidlgen.ReadName(string(c.Name))
	if err != nil {
		return nil, err
	}

	value := c.Value.Value
	expr := c.Value.Expression
	var ident *fidlgen.Name
	switch c.Value.Kind {
	case fidlgen.LiteralConstant:
		// In the integer case, the original expression conveys more
		// information than the equivalent decimal value: if the author
		// intended for the number to be understood in binary or hex, then the
		// generated code should preserve that.
		if kind == TypeKindInteger {
			value = expr
		}
		expr = ""
	case fidlgen.IdentifierConstant:
		identName, err := fidlgen.ReadName(string(c.Value.Identifier))
		if err != nil {
			return nil, err
		}
		ident = &identName
		expr = ""
	}

	return &Const{
		Kind:       kind,
		Type:       typ,
		Name:       name,
		Value:      value,
		Identifier: ident,
		Expression: expr,
		Comments:   c.DocComments(),
	}, nil
}

// Enum represents an FIDL enum declaration.
type Enum struct {
	// Name is the full name of the associated FIDL declaration.
	Name fidlgen.Name

	// The primitive subtype underlying the Enum.
	Subtype fidlgen.PrimitiveSubtype

	// Members is the list of member values of the enum.
	Members []EnumMember

	// Comments that comprise the original docstring of the FIDL declaration.
	Comments []string
}

// EnumMember represents a FIDL enum value.
type EnumMember struct {
	// Name is the name of the member.
	Name string

	// Value is the member's value.
	Value string

	// Comments that comprise the original docstring of the FIDL declaration.
	Comments []string
}

func newEnum(enum fidlgen.Enum) (*Enum, error) {
	name, err := fidlgen.ReadName(string(enum.Name))
	if err != nil {
		return nil, err
	}

	e := &Enum{
		Subtype:  enum.Type,
		Name:     name,
		Comments: enum.DocComments(),
	}
	for _, member := range enum.Members {
		e.Members = append(e.Members, EnumMember{
			Name:     string(member.Name),
			Value:    member.Value.Expression,
			Comments: member.DocComments(),
		})
	}
	return e, nil
}

// Bits represents an FIDL bitset declaration.
type Bits struct {
	// Name is the full name of the associated FIDL declaration.
	Name fidlgen.Name

	// The primitive subtype underlying the bitset.
	Subtype fidlgen.PrimitiveSubtype

	// Members is the list of member values of the bitset.
	Members []BitsMember

	// Comments that comprise the original docstring of the FIDL declaration.
	Comments []string
}

// BitsMember represents a FIDL enum value.
type BitsMember struct {
	// Name is the name of the member.
	Name string

	// Index is the associated bit index.
	Index int

	// Comments that comprise the original docstring of the FIDL declaration.
	Comments []string
}

func newBits(bits fidlgen.Bits) (*Bits, error) {
	name, err := fidlgen.ReadName(string(bits.Name))
	if err != nil {
		return nil, err
	}

	b := &Bits{
		Subtype:  bits.Type.PrimitiveSubtype,
		Name:     name,
		Comments: bits.DocComments(),
	}

	for _, member := range bits.Members {
		val, err := strconv.ParseUint(member.Value.Value, 10, 64)
		if err != nil {
			panic(fmt.Sprintf("%v member %s has bad value %q: %v", name, member.Name, member.Value.Value, err))
		}

		b.Members = append(b.Members, BitsMember{
			Name:     string(member.Name),
			Index:    log2(val),
			Comments: member.DocComments(),
		})
	}
	return b, nil
}

func log2(n uint64) int {
	if bits.OnesCount64(n) != 1 {
		panic(fmt.Sprintf("%d is not a power of two", n))
	}
	return bits.TrailingZeros64(n)
}

// TypeDescriptor gives a straightforward encoding of a type, accounting for
// any array nesting with a recursive pointer to a descriptor describing the
// element type.
type TypeDescriptor struct {
	// Type gives the full name of the type, except in the case of an array:
	// in that case, the array's element type is given by `.ElementType` and
	// its size is given by `.ElementCount`.
	Type string

	// Kind is the kind of the type.
	Kind TypeKind

	// ElementType gives the underlying element type in the case of an array.
	ElementType *TypeDescriptor

	// ElementCount gives the size of the associated array.
	ElementCount *int
}

func deriveType(typ fidlgen.Type, decls declMap, typeKinds map[TypeKind]struct{}) (*TypeDescriptor, error) {
	desc := TypeDescriptor{}
	switch typ.Kind {
	case fidlgen.PrimitiveType:
		if typ.PrimitiveSubtype.IsFloat() {
			return nil, fmt.Errorf("floats are unsupported")
		}
		desc.Type = string(typ.PrimitiveSubtype)
		if desc.Type == string(fidlgen.Bool) {
			desc.Kind = TypeKindBool
		} else {
			desc.Kind = TypeKindInteger
		}
	case fidlgen.StringType:
		return nil, fmt.Errorf("strings are only supported as constants")
	case fidlgen.IdentifierType:
		desc.Type = string(typ.Identifier)
		switch decls[desc.Type].(type) {
		case *fidlgen.Enum:
			desc.Kind = TypeKindEnum
		case *fidlgen.Bits:
			desc.Kind = TypeKindBits
		case *fidlgen.Struct:
			desc.Kind = TypeKindStruct
		default:
			return nil, fmt.Errorf("%s: unsupported declaration type: %s", desc.Type, decls[desc.Type])
		}
	case fidlgen.ArrayType:
		desc.Kind = TypeKindArray
		desc.ElementCount = typ.ElementCount
		nested, err := deriveType(*typ.ElementType, decls, typeKinds)
		if err != nil {
			return nil, err
		}
		desc.ElementType = nested
	default:
		return nil, fmt.Errorf("%s: unsupported type kind: %s", desc.Type, typ.Kind)
	}

	typeKinds[desc.Kind] = struct{}{}
	return &desc, nil
}

// Struct represents a FIDL struct declaration.
type Struct struct {
	// Name is the full name of the associated FIDL declaration.
	Name fidlgen.Name

	// Members is the list of the members of the layout.
	Members []StructMember

	// Comments that comprise the original docstring of the FIDL declaration.
	Comments []string
}

// StructMember represents a FIDL struct member.
type StructMember struct {
	// Name is the name of the member.
	Name string

	// Type describes the type of the member.
	Type TypeDescriptor

	// Comments that comprise the original docstring of the FIDL declaration.
	Comments []string
}

func newStruct(strct fidlgen.Struct, decls declMap, typeKinds map[TypeKind]struct{}) (*Struct, error) {
	if strct.IsAnonymous() {
		return nil, fmt.Errorf("anonymous structs are not allowed: %s", strct.Name)
	}

	name, err := fidlgen.ReadName(string(strct.Name))
	if err != nil {
		return nil, err
	}

	s := &Struct{
		Name:     name,
		Comments: strct.DocComments(),
	}
	for _, m := range strct.Members {
		typ, err := deriveType(m.Type, decls, typeKinds)
		if err != nil {
			return nil, fmt.Errorf("%s.%s: failed to derive type: %w", s.Name, m.Name, err)
		}
		s.Members = append(s.Members, StructMember{
			Name:     string(m.Name),
			Type:     *typ,
			Comments: m.DocComments(),
		})
	}
	return s, nil

}
