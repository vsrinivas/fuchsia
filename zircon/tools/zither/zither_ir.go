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

// Element represents a summarized FIDL element (i.e., a declaration or one of
// its members).
type Element interface {
	GetComments() []string
}

var _ = []Element{
	(*Const)(nil),
	(*Enum)(nil),
	(*EnumMember)(nil),
	(*Bits)(nil),
	(*BitsMember)(nil),
	(*Struct)(nil),
	(*StructMember)(nil),
	(*Alias)(nil),
	(*SyscallFamily)(nil),
	(*Syscall)(nil),
}

// Decl represents a summarized FIDL declaration.
type Decl interface {
	Element
	GetName() fidlgen.Name
}

var _ = []Decl{
	(*Const)(nil),
	(*Enum)(nil),
	(*Bits)(nil),
	(*Struct)(nil),
	(*Alias)(nil),
	(*SyscallFamily)(nil),
}

type decl struct {
	Comments []string
	Name     fidlgen.Name
}

func (d decl) GetComments() []string {
	return d.Comments
}

func (d decl) GetName() fidlgen.Name {
	return d.Name
}

func newDecl(d fidlgen.Decl) decl {
	return decl{
		Name:     fidlgen.MustReadName(string(d.GetName())),
		Comments: d.GetAttributes().DocComments(),
	}
}

// Member represents a summarized member of a FIDL layout declaration.
type Member interface {
	Element
	GetName() string
}

var _ = []Member{
	(*EnumMember)(nil),
	(*BitsMember)(nil),
	(*StructMember)(nil),
	(*Syscall)(nil),
}

type member struct {
	Comments []string
	Name     string
}

func (m member) GetComments() []string {
	return m.Comments
}

func (m member) GetName() string {
	return m.Name
}

func newMember(m fidlgen.Member) member {
	return member{
		Name:     string(m.GetName()),
		Comments: m.GetAttributes().DocComments(),
	}
}

// DeclWrapper represents an abstract (summarized) FIDL declaration, meant for
// use in template logic and featuring thin wrappers around type assertions for
// deriving concrete types. In normal go code, we would do the type assertions
// directly, but no can do in templates.
type DeclWrapper struct {
	value interface{}
}

func (decl DeclWrapper) Name() fidlgen.Name {
	return decl.AsDecl().GetName()
}

func (decl DeclWrapper) AsDecl() Decl {
	return decl.value.(Decl)
}

func (decl DeclWrapper) IsConst() bool {
	_, ok := decl.value.(*Const)
	return ok
}

func (decl DeclWrapper) AsConst() Const {
	return *decl.value.(*Const)
}

func (decl DeclWrapper) IsEnum() bool {
	_, ok := decl.value.(*Enum)
	return ok
}

func (decl DeclWrapper) AsEnum() Enum {
	return *decl.value.(*Enum)
}

func (decl DeclWrapper) IsBits() bool {
	_, ok := decl.value.(*Bits)
	return ok
}

func (decl DeclWrapper) AsBits() Bits {
	return *decl.value.(*Bits)
}

func (decl DeclWrapper) IsStruct() bool {
	_, ok := decl.value.(*Struct)
	return ok
}

func (decl DeclWrapper) AsStruct() Struct {
	return *decl.value.(*Struct)
}

func (decl DeclWrapper) IsAlias() bool {
	_, ok := decl.value.(*Alias)
	return ok
}

func (decl DeclWrapper) IsSyscallFamily() bool {
	_, ok := decl.value.(*SyscallFamily)
	return ok
}

func (decl DeclWrapper) AsAlias() Alias {
	return *decl.value.(*Alias)
}

func (decl DeclWrapper) AsSyscallFamily() SyscallFamily {
	return *decl.value.(*SyscallFamily)
}

// FileSummary is a summarized representation of a FIDL source file.
type FileSummary struct {
	// Library is the associated FIDL library.
	Library fidlgen.LibraryName

	// Source gives the associated source file.
	Source string

	// The contained declarations.
	Decls []DeclWrapper

	// See Deps().
	deps map[string]struct{}

	// See TypeKinds().
	typeKinds map[TypeKind]struct{}
}

// Name is the extension-less basename of the file.
func (summary FileSummary) Name() string {
	name := filepath.Base(summary.Source)
	name = strings.TrimSuffix(name, ".test.fidl")
	name = strings.TrimSuffix(name, ".fidl")
	name = strings.ReplaceAll(name, ".", "_")
	return name
}

// Deps records the (extension-less) file names that this file depends on; we
// say a file "depends" on another if the former has a declaration that depends
// on a declaration in the latter.
func (summary FileSummary) Deps() []string {
	var deps []string
	for dep := range summary.deps {
		deps = append(deps, dep)
	}
	// Sort to account for map access nondeterminism.
	sort.Strings(deps)
	return deps
}

// TypeKinds gives the kinds of types contained in this file's declarations.
// This is useful for knowing the precise set of imports to make in the code
// generated from this file.
func (summary FileSummary) TypeKinds() []TypeKind {
	var kinds []TypeKind
	for kind := range summary.typeKinds {
		kinds = append(kinds, kind)
	}
	// Sort to account for map access nondeterminism.
	sort.Slice(kinds, func(i, j int) bool {
		return strings.Compare(string(kinds[i]), string(kinds[j])) < 0
	})
	return kinds
}

type declMap map[string]Decl
type memberMap map[string]Member

// Summarize creates FIDL file summaries from FIDL IR. Within each file
// summary, declarations are ordered according to `order`.
func Summarize(ir fidlgen.Root, sourceDir string, order DeclOrder) ([]FileSummary, error) {
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
	fidlDecls := g.SortedDecls()

	locations := make(map[string]fidlgen.Location)
	processedDecls := make(declMap)
	processedConstMembers := make(memberMap)

	filesByName := make(map[string]*FileSummary)
	getFile := func(location fidlgen.Location) *FileSummary {
		src, err := filepath.Rel(sourceDir, location.Filename)
		if err != nil {
			panic(fmt.Sprintf("could not relativize %s against source directory %s: %s", location.Filename, sourceDir, err))
		}

		file, ok := filesByName[location.Filename]
		if !ok {
			file = &FileSummary{
				Library:   libName,
				Source:    src,
				deps:      make(map[string]struct{}),
				typeKinds: make(map[TypeKind]struct{}),
			}
			filesByName[location.Filename] = file
		}
		return file
	}

	for _, fidlDecl := range fidlDecls {
		typeKinds := make(map[TypeKind]struct{})
		var decl Decl
		var constMembers []Member
		var err error
		switch fidlDecl := fidlDecl.(type) {
		case *fidlgen.Const:
			decl, err = newConst(*fidlDecl, processedDecls, processedConstMembers)
			if err == nil {
				typeKinds[decl.(*Const).Kind] = struct{}{}
			}
		case *fidlgen.Enum:
			decl, err = newEnum(*fidlDecl)
			if err == nil {
				for _, m := range decl.(*Enum).Members {
					constMembers = append(constMembers, Member(m))
				}
				typeKinds[TypeKindInteger] = struct{}{}
			}
		case *fidlgen.Bits:
			decl, err = newBits(*fidlDecl)
			if err == nil {
				for _, m := range decl.(*Bits).Members {
					constMembers = append(constMembers, Member(m))
				}
				typeKinds[TypeKindInteger] = struct{}{}
			}
		case *fidlgen.Struct:
			decl, err = newStruct(*fidlDecl, processedDecls, typeKinds)
		case *fidlgen.Alias:
			decl, err = newAlias(*fidlDecl, processedDecls, typeKinds)
		case *fidlgen.Protocol:
			decl, err = newSyscallFamily(*fidlDecl)
		}
		if err != nil {
			return nil, err
		}

		// A nil `decl` means that it corresponds to an unknown FIDL
		// declaration. Moreover, if `decl` wraps a nil (which does not imply
		// that it is nil itself), then that means that it is defined in terms
		// of unknown elements. In either case, we cannot make sense of the
		// definition and so we skip it.
		//
		// TODO(fxbug.dev/106538): We do not want to silently ignore things
		// that a user might expect to generate results.
		if decl == nil || reflect.ValueOf(decl).IsNil() {
			// Before skipping, we should still record the file that the
			// unrecognized declaration came from (e.g., for determinism in
			// the case where we wish the have the generated set of files
			// match the incoming source file names); calling getFile() has
			// this side-effect.
			getFile(fidlDecl.GetLocation())
			continue
		}

		file := getFile(fidlDecl.GetLocation())
		file.Decls = append(file.Decls, DeclWrapper{decl})
		for kind := range typeKinds {
			file.typeKinds[kind] = struct{}{}
		}

		// Now go back and record the dependents' dependency on this declaration.
		dependents, ok := g.GetDirectDependents(fidlDecl.GetName())
		if !ok {
			panic(fmt.Sprintf("%s not found in declaration graph", decl.GetName()))
		}
		for _, dependent := range dependents {
			dependentFile := getFile(dependent.GetLocation())
			if dependentFile.Source != file.Source {
				dependentFile.deps[file.Name()] = struct{}{}
			}
		}

		declName := decl.GetName().String()
		locations[declName] = fidlDecl.GetLocation()
		processedDecls[declName] = decl
		for _, m := range constMembers {
			processedConstMembers[declName+"."+m.GetName()] = m
		}
	}

	var files []FileSummary
	for _, file := range filesByName {
		// Now reorder declarations in the order expected by the backends.
		switch order {
		case SourceDeclOrder:
			sort.Slice(file.Decls, func(i, j int) bool {
				locI := locations[file.Decls[i].Name().String()]
				locJ := locations[file.Decls[j].Name().String()]
				return fidlgen.LocationCmp(locI, locJ)
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
	decl

	// Kind is the kind of the constant's type.
	Kind TypeKind

	// Type is the FIDL type of the constant. If Kind is TypeKindEnum or
	// TypeKindBits, then this field encodes a full declaration name of
	// the associated enum or bits.
	Type string

	// Value is the constant's value in string form.
	Value string

	// Element gives whether this constant was defined in terms of another FIDL
	// element.
	Element *ConstElementValue

	// Expression is the original FIDL expression given for the value,
	// included only when it meaningfully differs from the value.
	Expression string
}

func newConst(c fidlgen.Const, decls declMap, members memberMap) (*Const, error) {
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
		case *Enum:
			kind = TypeKindEnum
		case *Bits:
			kind = TypeKindBits
		default:
			return nil, fmt.Errorf("%v has unsupported constant type: %s", c.Name, reflect.TypeOf(decls[typ]).Name())
		}
	default:
		return nil, fmt.Errorf("%v has unsupported constant type: %s", c.Name, c.Type.Kind)
	}

	value := c.Value.Value
	expr := c.Value.Expression
	var elVal *ConstElementValue
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
		valName, err := fidlgen.ReadName(string(c.Value.Identifier))
		if err != nil {
			return nil, err
		}
		declName, memberName := valName.SplitMember()
		elVal = &ConstElementValue{
			Decl: decls[declName.String()],
		}
		if memberName != "" {
			elVal.Member = members[valName.String()]
		}
		expr = ""
	case fidlgen.BinaryOperator:
		decl, ok := decls[typ]
		if !ok {
			break
		}
		elVal = &ConstElementValue{Decl: decl}
	}

	return &Const{
		decl:       newDecl(c),
		Kind:       kind,
		Type:       typ,
		Value:      value,
		Element:    elVal,
		Expression: expr,
	}, nil
}

// ConstElementValue represents a constant value given by another FIDL element.
type ConstElementValue struct {
	// Decl either gives either another constant or the parent layout of an
	// enum or bits member.
	Decl

	// Member - if non-nil - gives the member of Decl() defining the associated
	// constant.
	Member
}

// Enum represents an FIDL enum declaration.
type Enum struct {
	decl

	// The primitive subtype underlying the Enum.
	Subtype fidlgen.PrimitiveSubtype

	// Members is the list of member values of the enum.
	Members []EnumMember
}

// EnumMember represents a FIDL enum value.
type EnumMember struct {
	member

	// Value is the member's value.
	Value string
}

func newEnum(enum fidlgen.Enum) (*Enum, error) {
	e := &Enum{
		decl:    newDecl(enum),
		Subtype: enum.Type,
	}
	for _, member := range enum.Members {
		e.Members = append(e.Members, EnumMember{
			member: newMember(member),
			Value:  member.Value.Expression,
		})
	}
	return e, nil
}

// Bits represents an FIDL bitset declaration.
type Bits struct {
	decl

	// The primitive subtype underlying the bitset.
	Subtype fidlgen.PrimitiveSubtype

	// Members is the list of member values of the bitset.
	Members []BitsMember
}

// BitsMember represents a FIDL enum value.
type BitsMember struct {
	member

	// Name is the name of the member.
	Name string

	// Index is the associated bit index.
	Index int
}

func newBits(bits fidlgen.Bits) (*Bits, error) {
	b := &Bits{
		decl:    newDecl(bits),
		Subtype: bits.Type.PrimitiveSubtype,
	}

	for _, member := range bits.Members {
		val, err := strconv.ParseUint(member.Value.Value, 10, 64)
		if err != nil {
			panic(fmt.Sprintf("%v member %s has bad value %q: %v", b.Name, member.Name, member.Value.Value, err))
		}

		b.Members = append(b.Members, BitsMember{
			member: newMember(member),
			Index:  log2(val),
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

	// Decl gives the associated declaration, if one exists.
	Decl Decl

	// Kind is the kind of the type.
	Kind TypeKind

	// ElementType gives the underlying element type in the case of an array.
	ElementType *TypeDescriptor

	// ElementCount gives the size of the associated array.
	ElementCount *int
}

// Represents an recursively-defined type, effectively abstracting
// fidlgen.Type and fidlgen.PartialTypeConstructor.
type recursiveType interface {
	GetKind() fidlgen.TypeKind
	GetPrimitiveSubtype() fidlgen.PrimitiveSubtype
	GetIdentifierType() fidlgen.EncodedCompoundIdentifier
	GetElementType() recursiveType
	GetElementCount() *int
}

// Resolves a recursively-defined type into a type descriptor. This process
// requires a map of previously processed declarations for consulting, as well
// as map of type kinds to record those seen during the resolution.
func resolveType(typ recursiveType, decls declMap, typeKinds map[TypeKind]struct{}) (*TypeDescriptor, error) {
	desc := TypeDescriptor{}
	switch typ.GetKind() {
	case fidlgen.PrimitiveType:
		if typ.GetPrimitiveSubtype().IsFloat() {
			return nil, fmt.Errorf("floats are unsupported")
		}
		desc.Type = string(typ.GetPrimitiveSubtype())
		if desc.Type == string(fidlgen.Bool) {
			desc.Kind = TypeKindBool
		} else {
			desc.Kind = TypeKindInteger
		}
	case fidlgen.StringType:
		return nil, fmt.Errorf("strings are only supported as constants")
	case fidlgen.IdentifierType:
		desc.Type = string(typ.GetIdentifierType())
		desc.Decl = decls[desc.Type]
		switch desc.Decl.(type) {
		case *Enum:
			desc.Kind = TypeKindEnum
		case *Bits:
			desc.Kind = TypeKindBits
		case *Struct:
			desc.Kind = TypeKindStruct
		default: // TODO(fxbug.dev/106538): Skip if unknown.
			return nil, nil
		}

	case fidlgen.ArrayType:
		desc.Kind = TypeKindArray
		desc.ElementCount = typ.GetElementCount()
		nested, err := resolveType(typ.GetElementType(), decls, typeKinds)
		if err != nil {
			return nil, err
		}
		if nested == nil { // TODO(fxbug.dev/106538): Skip if unknown.
			return nil, nil
		}
		desc.ElementType = nested
	default: // TODO(fxbug.dev/106538): Skip if unknown.
		return nil, nil
	}

	typeKinds[desc.Kind] = struct{}{}
	return &desc, nil
}

// A thin wrapper implementing `recursiveType`.
type fidlgenType fidlgen.Type

func (typ fidlgenType) GetKind() fidlgen.TypeKind { return typ.Kind }

func (typ fidlgenType) GetPrimitiveSubtype() fidlgen.PrimitiveSubtype {
	return typ.PrimitiveSubtype
}

func (typ fidlgenType) GetIdentifierType() fidlgen.EncodedCompoundIdentifier {
	return typ.Identifier
}

func (typ fidlgenType) GetElementType() recursiveType { return fidlgenType(*typ.ElementType) }

func (typ fidlgenType) GetElementCount() *int { return typ.ElementCount }

// Struct represents a FIDL struct declaration.
type Struct struct {
	decl

	// Size is the size of the struct, including padding.
	Size int

	// Members is the list of the members of the layout.
	Members []StructMember
}

// StructMember represents a FIDL struct member.
type StructMember struct {
	member

	// Type describes the type of the member.
	Type TypeDescriptor

	// Offset is the offset of the field.
	Offset int
}

func newStruct(strct fidlgen.Struct, decls declMap, typeKinds map[TypeKind]struct{}) (*Struct, error) {
	if strct.IsAnonymous() {
		return nil, nil
	}

	s := &Struct{
		decl: newDecl(strct),
		Size: strct.TypeShapeV2.InlineSize,
	}
	for _, member := range strct.Members {
		typ, err := resolveType(fidlgenType(member.Type), decls, typeKinds)
		if err != nil {
			return nil, fmt.Errorf("%s.%s: failed to derive type: %w", s.Name, member.Name, err)
		}
		if typ == nil { // TODO(fxbug.dev/106538): Skip if unknown.
			return nil, nil
		}
		s.Members = append(s.Members, StructMember{
			member: newMember(member),
			Type:   *typ,
			Offset: member.FieldShapeV2.Offset,
		})
	}
	return s, nil

}

// Alias represents a FIDL type alias declaration.
type Alias struct {
	decl

	// Value is the type under alias (i.e., the right-hand side of the
	// declaration).
	Value TypeDescriptor
}

func newAlias(alias fidlgen.Alias, decls declMap, typeKinds map[TypeKind]struct{}) (*Alias, error) {
	unresolved := fidlgenTypeCtor(alias.PartialTypeConstructor)
	typ, err := resolveType(unresolved, decls, typeKinds)
	if err != nil {
		return nil, err
	}
	if typ == nil { // TODO(fxbug.dev/106538): Skip if unknown.
		return nil, nil
	}
	return &Alias{
		decl:  newDecl(alias),
		Value: *typ,
	}, nil
}

// An implementation of `recursiveType`.
type fidlgenTypeCtor fidlgen.PartialTypeConstructor

func (ctor fidlgenTypeCtor) GetKind() fidlgen.TypeKind {
	if _, err := fidlgen.ReadName(string(ctor.Name)); err == nil {
		return fidlgen.IdentifierType
	}

	switch string(ctor.Name) {
	case string(fidlgen.Bool),
		string(fidlgen.Int8),
		string(fidlgen.Int16),
		string(fidlgen.Int32),
		string(fidlgen.Int64),
		string(fidlgen.Uint8),
		string(fidlgen.Uint16),
		string(fidlgen.Uint32),
		string(fidlgen.Uint64),
		string(fidlgen.Float32),
		string(fidlgen.Float64):
		return fidlgen.PrimitiveType
	case "string":
		return fidlgen.StringType
	case "array":
		return fidlgen.ArrayType
	default:
		panic(fmt.Sprintf("unsupported type: %s", ctor.Name))
	}
}

func (ctor fidlgenTypeCtor) GetPrimitiveSubtype() fidlgen.PrimitiveSubtype {
	return fidlgen.PrimitiveSubtype(ctor.Name)
}

func (ctor fidlgenTypeCtor) GetIdentifierType() fidlgen.EncodedCompoundIdentifier {
	return ctor.Name
}

func (ctor fidlgenTypeCtor) GetElementType() recursiveType {
	if len(ctor.Args) == 0 {
		return nil
	}
	// TODO(fxbug.dev/7660): This list appears to always be empty or a
	// singleton (and its unclear what further arguments would mean).
	return fidlgenTypeCtor(ctor.Args[0])
}

func (ctor fidlgenTypeCtor) GetElementCount() *int {
	if ctor.MaybeSize == nil {
		return nil
	}
	count, err := strconv.Atoi(ctor.MaybeSize.Value)
	if err != nil {
		panic(fmt.Sprintf("could not interpret %s as an int", ctor.MaybeSize.Value))
	}
	return &count
}

// SyscallFamily represents a logical family of syscalls, usually corresponding
// to a kernel object and the noun phrase namespacing in
// `zx_${noun_phrase}_${verb_phrase}`.
//
// The interpretation here relies upon the conventions detailed in
// //zircon/vdso/README.md.
type SyscallFamily struct {
	decl

	// Syscalls gives the syscalls that comprise the family.
	Syscalls []Syscall
}

// SyscallCategory gives a categorization of syscalls.
type SyscallCategory string

const (
	// SyscallCategoryBasic indicates the basic, unqualified category of
	// syscalls (which is the majority).
	SyscallCategoryBasic SyscallCategory = ""

	// SyscallCategoryInternal indicates that the syscall is not part of public
	// ABI.
	//
	// See the definition of @internal in //zircon/vdso/README.md.
	SyscallCategoryInternal SyscallCategory = "internal"

	// SyscallCategoryVdsocall indicates that the syscall is not a true
	// syscall and is actually implemented within the vDSO.
	//
	// See the definition of @vdsocall in //zircon/vdso/README.md.
	SyscallCategoryVdsoCall SyscallCategory = "vdsocall"

	// SyscallCategoryNext indicates that the syscall is in the process of
	// being stabilized and is not yet a part of public ABI.
	//
	// See the definition of @next in //zircon/vdso/README.md.
	SyscallCategoryNext SyscallCategory = "next"

	// SyscallCategoryTest1 is a category relevant only to testing.
	//
	// See the definition of @test_category1 in //zircon/vdso/README.md.
	SyscallCategoryTest1 SyscallCategory = "test_category1"

	// SyscallCategoryTest2 is a category relevant only to testing.
	//
	// See the definition of @test_category2 in //zircon/vdso/README.md.
	SyscallCategoryTest2 SyscallCategory = "test_category2"
)

// Syscall corresponds to an individual syscall.
//
// `Name` includes the family namespacing of the syscall (i.e., "PortWait"
// instead of just "Wait").
type Syscall struct {
	member

	// Category gives the syscall's category.
	Category SyscallCategory

	// Blocking indicates a blocking call.
	//
	// See the definition of @blocking in //zircon/vdso/README.md.
	Blocking bool

	// NoReturn indicates that the sycall does not return.
	//
	// See the definition of @noreturn in //zircon/vdso/README.md.
	NoReturn bool

	// Const, which only applies to SyscallCategoryVdsoCall syscalls, indicates
	// that the function is "const" in the sense of
	// `__attribute__((__const__))`.
	//
	// See the definition of @const in //zircon/vdso/README.md.
	Const bool

	// Whether the syscall is defined solely for tests.
	//
	// See the definition of @testonly in //zircon/vdso/README.md.
	Testonly bool
}

func (syscall Syscall) IsInternal() bool      { return syscall.Category == SyscallCategoryInternal }
func (syscall Syscall) IsVdsoCall() bool      { return syscall.Category == SyscallCategoryVdsoCall }
func (syscall Syscall) IsNext() bool          { return syscall.Category == SyscallCategoryNext }
func (syscall Syscall) IsTestCategory1() bool { return syscall.Category == SyscallCategoryTest1 }
func (syscall Syscall) IsTestCategory2() bool { return syscall.Category == SyscallCategoryTest2 }

func newSyscallFamily(protocol fidlgen.Protocol) (*SyscallFamily, error) {
	if protocol.OverTransport() != "Syscall" {
		return nil, nil
	}
	family := &SyscallFamily{decl: newDecl(protocol)}
	// This gives whether the family protocol name actually meaningfully
	// signals the official namespacing of the syscalls. If the attribute is
	// present, the name is arbitrary and the syscall member names already
	// include the proper namespacing.
	//
	// See @no_protocol_prefix in //zircon/vdso/README.md
	_, noProtocolPrefix := protocol.LookupAttribute("no_protocol_prefix")
	for _, method := range protocol.Methods {

		syscall := Syscall{
			member:   newMember(method),
			Category: SyscallCategoryBasic,
		}
		if !noProtocolPrefix {
			syscall.member.Name = family.Name.DeclarationName() + syscall.member.Name
		}
		for _, cat := range []SyscallCategory{
			SyscallCategoryInternal,
			SyscallCategoryVdsoCall,
			SyscallCategoryNext,
			SyscallCategoryTest1,
			SyscallCategoryTest2,
		} {
			if _, ok := method.LookupAttribute(fidlgen.Identifier(cat)); !ok {
				continue
			}
			if syscall.Category != SyscallCategoryBasic {
				return nil, fmt.Errorf("syscall %s cannot be annotated with both @%s and @%s", syscall.Name, syscall.Category, cat)
			}
			syscall.Category = cat
		}

		_, syscall.Blocking = method.LookupAttribute("blocking")
		_, syscall.NoReturn = method.LookupAttribute("noreturn")
		_, syscall.Const = method.LookupAttribute("const")
		_, syscall.Testonly = method.LookupAttribute("testonly")

		// @const must be paired with @vdsocall.
		if syscall.Const && !syscall.IsVdsoCall() {
			return nil, fmt.Errorf("annotation @const on syscall %s must be paired with @vdsocall", syscall.Name)
		}

		// @test_category{1,2} must be paired with @testonly
		if (syscall.IsTestCategory1() || syscall.IsTestCategory2()) && !syscall.Testonly {
			return nil, fmt.Errorf("annotation @%s on syscall %s must be paired with @testonly", syscall.Category, syscall.Name)
		}

		family.Syscalls = append(family.Syscalls, syscall)
	}
	return family, nil
}
