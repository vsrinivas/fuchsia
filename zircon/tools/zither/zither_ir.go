// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package zither contains abstractions and utilities shared by the various backends.
package zither

import (
	"fmt"
	"math/bits"
	"reflect"
	"sort"
	"strconv"

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

// Summary is a summarized representation of a FIDL library's set of
// declarations. It contains the minimal subset of information needed for the
// zither backends in a maximally convenient form.
type Summary struct {
	Name  fidlgen.LibraryName
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

type declMap map[string]fidlgen.Declaration

// NewSummary creates a Summary from FIDL IR. The resulting list of
// declarations are ordered according to `order`.
func NewSummary(ir fidlgen.Root, order DeclOrder) (*Summary, error) {
	name, err := fidlgen.ReadLibraryName(string(ir.Name))
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

	s := &Summary{Name: name}
	for _, decl := range decls {
		var summarized interface{}
		var err error
		switch decl := decl.(type) {
		case *fidlgen.Const:
			summarized, err = newConst(*decl, processed)
		case *fidlgen.Enum:
			summarized, err = newEnum(*decl)
		case *fidlgen.Bits:
			summarized, err = newBits(*decl)
		default:
			return nil, fmt.Errorf("unsupported declaration type: %s", fidlgen.GetDeclType(decl))
		}
		if err != nil {
			return nil, err
		}
		s.Decls = append(s.Decls, Decl{summarized})
		processed[string(decl.GetName())] = decl
	}

	// Now reorder declarations in the order expected by the backends.
	switch order {
	case SourceDeclOrder:
		sort.Slice(s.Decls, func(i, j int) bool {
			ith := processed[s.Decls[i].Name().String()]
			jth := processed[s.Decls[j].Name().String()]
			return fidlgen.LocationCmp(ith.GetLocation(), jth.GetLocation())
		})
	case DependencyDeclOrder:
		// Already in this order.
	default:
		panic(fmt.Sprintf("unknown declaration order: %v", order))
	}

	return s, nil
}

// TypeKind gives a rough categorization of FIDL primitive and declaration types.
type TypeKind string

const (
	TypeKindBool    TypeKind = "bool"
	TypeKindInteger TypeKind = "integer"
	TypeKindString  TypeKind = "string"
	TypeKindEnum    TypeKind = "enum"
	TypeKindBits    TypeKind = "bits"
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
