// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"sort"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type declKind namespacedEnumMember

type declKinds struct {
	Bits     declKind
	Const    declKind
	Enum     declKind
	Protocol declKind
	Service  declKind
	Struct   declKind
	Table    declKind
	Union    declKind
}

// Kinds are the different kinds of FIDL declarations. They are used in
// header/impl templates to select the correct decl-specific template.
var Kinds = namespacedEnum(declKinds{}).(declKinds)

// A Kinded value is a declaration in FIDL, for which we would like to
// generate some corresponding C++ code.
type Kinded interface {
	Kind() declKind
}

type familyKind namespacedEnumMember

type familyKinds struct {
	// TrivialCopy identifies values for whom a copy is trivial (like integers)
	TrivialCopy familyKind

	// Reference identifies values with a non trivial copy for which we use a
	// reference on the caller argument.
	Reference familyKind

	// String identifies string values for which we can use a const reference
	// and for which we can optimize the field construction.
	String familyKind

	// Vector identifies vector values for which we can use a reference and for
	// which we can optimize the field construction.
	Vector familyKind
}

// FamilyKinds are general categories identifying what operation we should use
// to pass a value without a move (LLCPP). It also defines the way we should
// initialize a field.
var FamilyKinds = namespacedEnum(familyKinds{}).(familyKinds)

type typeKind namespacedEnumMember

type typeKinds struct {
	Array     typeKind
	Vector    typeKind
	String    typeKind
	Handle    typeKind
	Request   typeKind
	Primitive typeKind
	Bits      typeKind
	Enum      typeKind
	Const     typeKind
	Struct    typeKind
	Table     typeKind
	Union     typeKind
	Protocol  typeKind
}

// TypeKinds are the kinds of C++ types (arrays, primitives, structs, ...).
var TypeKinds = namespacedEnum(typeKinds{}).(typeKinds)

type Type struct {
	NameVariants

	WirePointer bool

	// Defines what operation we should use to pass a value without a move (LLCPP). It also
	// defines the way we should initialize a field.
	WireFamily familyKind

	// NeedsDtor indicates whether this type needs to be destructed explicitely
	// or not.
	NeedsDtor bool

	Kind typeKind

	IsResource bool

	DeclarationName fidlgen.EncodedCompoundIdentifier

	// Set iff IsArray || IsVector
	ElementType *Type
	// Valid iff IsArray
	ElementCount int
}

// IsPrimitiveType returns true if this type is primitive.
func (t *Type) IsPrimitiveType() bool {
	return t.Kind == TypeKinds.Primitive || t.Kind == TypeKinds.Bits || t.Kind == TypeKinds.Enum
}

// WireArgumentDeclaration returns the argument declaration for this type for the wire variant.
func (t *Type) WireArgumentDeclaration(n string) string {
	switch t.WireFamily {
	case FamilyKinds.TrivialCopy:
		return t.String() + " " + n
	case FamilyKinds.Reference, FamilyKinds.Vector:
		return t.String() + "& " + n
	case FamilyKinds.String:
		return "const " + t.String() + "& " + n
	default:
		panic(fmt.Sprintf("Unknown wire family kind %v", t.WireFamily))
	}
}

// WireInitMessage returns message field initialization for the wire variant.
func (t *Type) WireInitMessage(n string) string {
	switch t.WireFamily {
	case FamilyKinds.TrivialCopy:
		return fmt.Sprintf("%s(%s)", n, n)
	case FamilyKinds.Reference:
		return fmt.Sprintf("%s(std::move(%s))", n, n)
	case FamilyKinds.String:
		return fmt.Sprintf("%s(::fidl::unowned_ptr_t<const char>(%s.data()), %s.size())",
			n, n, n)
	case FamilyKinds.Vector:
		return fmt.Sprintf("%s(::fidl::unowned_ptr_t<%s>(%s.mutable_data()), %s.count())",
			n, t.ElementType, n, n)
	default:
		panic(fmt.Sprintf("Unknown wire family kind %v", t.WireFamily))

	}
}

type Member interface {
	NameAndType() (string, Type)
}

type Root struct {
	PrimaryHeader   string
	IncludeStem     string
	Headers         []string
	FuzzerHeaders   []string
	HandleTypes     []string
	Library         fidlgen.LibraryIdentifier
	LibraryReversed fidlgen.LibraryIdentifier
	Decls           []Kinded
}

// Holds information about error results on methods
type Result struct {
	ValueMembers    []Parameter
	ResultDecl      NameVariants
	ErrorDecl       NameVariants
	ValueDecl       Name
	ValueStructDecl NameVariants
	ValueTupleDecl  Name
}

func (r Result) ValueArity() int {
	return len(r.ValueMembers)
}

var primitiveTypes = map[fidlgen.PrimitiveSubtype]string{
	fidlgen.Bool:    "bool",
	fidlgen.Int8:    "int8_t",
	fidlgen.Int16:   "int16_t",
	fidlgen.Int32:   "int32_t",
	fidlgen.Int64:   "int64_t",
	fidlgen.Uint8:   "uint8_t",
	fidlgen.Uint16:  "uint16_t",
	fidlgen.Uint32:  "uint32_t",
	fidlgen.Uint64:  "uint64_t",
	fidlgen.Float32: "float",
	fidlgen.Float64: "double",
}

// NameVariantsForPrimitive returns the C++ name of a FIDL primitive type.
func NameVariantsForPrimitive(val fidlgen.PrimitiveSubtype) NameVariants {
	if t, ok := primitiveTypes[val]; ok {
		return PrimitiveNameVariants(t)
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

type identifierTransform bool

const (
	keepPartIfReserved   identifierTransform = false
	changePartIfReserved identifierTransform = true
)

func libraryParts(library fidlgen.LibraryIdentifier, identifierTransform identifierTransform) []string {
	parts := []string{}
	for _, part := range library {
		if identifierTransform == changePartIfReserved {
			parts = append(parts, changeIfReserved(part))
		} else {
			parts = append(parts, string(part))
		}
	}
	return parts
}

func formatLibraryPrefix(library fidlgen.LibraryIdentifier) string {
	return formatLibrary(library, "_", keepPartIfReserved)
}

func formatLibraryPath(library fidlgen.LibraryIdentifier) string {
	return formatLibrary(library, "/", keepPartIfReserved)
}

type libraryNamespaceFunc func(fidlgen.LibraryIdentifier) Namespace

func codingTableName(ident fidlgen.EncodedCompoundIdentifier) string {
	ci := fidlgen.ParseCompoundIdentifier(ident)
	return formatLibrary(ci.Library, "_", keepPartIfReserved) + "_" + string(ci.Name) + string(ci.Member)
}

type compiler struct {
	symbolPrefix    string
	decls           fidlgen.DeclInfoMap
	library         fidlgen.LibraryIdentifier
	handleTypes     map[fidlgen.HandleSubtype]struct{}
	resultForStruct map[fidlgen.EncodedCompoundIdentifier]*Result
	resultForUnion  map[fidlgen.EncodedCompoundIdentifier]*Result
}

func (c *compiler) isInExternalLibrary(ci fidlgen.CompoundIdentifier) bool {
	if len(ci.Library) != len(c.library) {
		return true
	}
	for i, part := range c.library {
		if ci.Library[i] != part {
			return true
		}
	}
	return false
}

func (c *compiler) compileNameVariants(eci fidlgen.EncodedCompoundIdentifier) NameVariants {
	ci := fidlgen.ParseCompoundIdentifier(eci)
	if ci.Member != fidlgen.Identifier("") {
		panic(fmt.Sprintf("unexpected compound identifier with member: %v", eci))
	}
	name := changeIfReserved(ci.Name)
	declInfo, ok := c.decls[eci]
	if !ok {
		panic(fmt.Sprintf("unknown identifier: %v", eci))
	}
	declType := declInfo.Type
	switch declType {
	case fidlgen.ConstDeclType, fidlgen.BitsDeclType, fidlgen.EnumDeclType, fidlgen.StructDeclType, fidlgen.TableDeclType, fidlgen.UnionDeclType:
		return NameVariants{
			Natural: naturalNamespace(ci.Library).Member(name),
			Wire:    wireNamespace(ci.Library).Member(name),
		}
	case fidlgen.ProtocolDeclType, fidlgen.ServiceDeclType:
		return NameVariants{
			Natural: naturalNamespace(ci.Library).Member(name),
			Wire:    unifiedNamespace(ci.Library).Member(name),
		}
	}
	panic("Unknown decl type: " + string(declType))
}

func (c *compiler) compileCodingTableType(eci fidlgen.EncodedCompoundIdentifier) string {
	val := fidlgen.ParseCompoundIdentifier(eci)
	if c.isInExternalLibrary(val) {
		panic(fmt.Sprintf("can't create coding table type for external identifier: %v", val))
	}

	return fmt.Sprintf("%s_%sTable", c.symbolPrefix, val.Name)
}

func (c *compiler) compileType(val fidlgen.Type) Type {
	r := Type{}
	switch val.Kind {
	case fidlgen.ArrayType:
		t := c.compileType(*val.ElementType)
		r.NameVariants.Natural = MakeName("std::array").ArrayTemplate(t.Natural, *val.ElementCount)
		r.NameVariants.Wire = MakeName("fidl::Array").ArrayTemplate(t.Wire, *val.ElementCount)
		r.WirePointer = t.WirePointer
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Array
		r.IsResource = t.IsResource
		r.ElementType = &t
		r.ElementCount = *val.ElementCount
	case fidlgen.VectorType:
		t := c.compileType(*val.ElementType)
		if val.Nullable {
			r.NameVariants.Natural = MakeName("fidl::VectorPtr").Template(t.Natural)
		} else {
			r.NameVariants.Natural = MakeName("std::vector").Template(t.Natural)
		}
		r.NameVariants.Wire = MakeName("fidl::VectorView").Template(t.Wire)
		r.WireFamily = FamilyKinds.Vector
		r.WirePointer = t.WirePointer
		r.NeedsDtor = true
		r.Kind = TypeKinds.Vector
		r.IsResource = t.IsResource
		r.ElementType = &t
	case fidlgen.StringType:
		r.Wire = MakeName("fidl::StringView")
		r.WireFamily = FamilyKinds.String
		if val.Nullable {
			r.Natural = MakeName("fidl::StringPtr")
		} else {
			r.Natural = MakeName("std::string")
		}
		r.NeedsDtor = true
		r.Kind = TypeKinds.String
	case fidlgen.HandleType:
		c.handleTypes[val.HandleSubtype] = struct{}{}
		r.NameVariants = NameVariantsForHandle(val.HandleSubtype)
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Handle
		r.IsResource = true
	case fidlgen.RequestType:
		p := c.compileNameVariants(val.RequestSubtype)
		r.NameVariants = NameVariants{
			Natural: MakeName("fidl::InterfaceRequest").Template(p.Natural),
			Wire:    MakeName("fidl::ServerEnd").Template(p.Wire),
		}
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Request
		r.IsResource = true
	case fidlgen.PrimitiveType:
		r.NameVariants = NameVariantsForPrimitive(val.PrimitiveSubtype)
		r.WireFamily = FamilyKinds.TrivialCopy
		r.Kind = TypeKinds.Primitive
	case fidlgen.IdentifierType:
		name := c.compileNameVariants(val.Identifier)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		declType := declInfo.Type
		if declType == fidlgen.ProtocolDeclType {
			r.NameVariants.Natural = MakeName("fidl::InterfaceHandle").Template(name.Natural)
			r.NameVariants.Wire = MakeName("fidl::ClientEnd").Template(name.Wire)
			r.WireFamily = FamilyKinds.Reference
			r.NeedsDtor = true
			r.Kind = TypeKinds.Protocol
			r.IsResource = true
		} else {
			switch declType {
			case fidlgen.BitsDeclType:
				r.Kind = TypeKinds.Bits
				r.WireFamily = FamilyKinds.TrivialCopy
			case fidlgen.EnumDeclType:
				r.Kind = TypeKinds.Enum
				r.WireFamily = FamilyKinds.TrivialCopy
			case fidlgen.ConstDeclType:
				r.Kind = TypeKinds.Const
				r.WireFamily = FamilyKinds.Reference
			case fidlgen.StructDeclType:
				r.Kind = TypeKinds.Struct
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.WirePointer = val.Nullable
				r.IsResource = declInfo.IsResourceType()
			case fidlgen.TableDeclType:
				r.Kind = TypeKinds.Table
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.WirePointer = val.Nullable
				r.IsResource = declInfo.IsResourceType()
			case fidlgen.UnionDeclType:
				r.Kind = TypeKinds.Union
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.IsResource = declInfo.IsResourceType()
			default:
				panic(fmt.Sprintf("unknown declaration type: %v", declType))
			}

			if val.Nullable {
				r.NameVariants.Natural = MakeName("std::unique_ptr").Template(name.Natural)
				if declType == fidlgen.UnionDeclType {
					r.NameVariants.Wire = name.Wire
				} else {
					r.NameVariants.Wire = MakeName("fidl::ObjectView").Template(name.Wire)
				}
				r.NeedsDtor = true
			} else {
				r.NameVariants = name
				r.NeedsDtor = true
			}
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}
	return r
}

func compile(r fidlgen.Root) Root {
	root := Root{}
	library := make(fidlgen.LibraryIdentifier, 0)
	rawLibrary := make(fidlgen.LibraryIdentifier, 0)
	for _, identifier := range fidlgen.ParseLibraryName(r.Name) {
		safeName := changeIfReserved(identifier)
		library = append(library, fidlgen.Identifier(safeName))
		rawLibrary = append(rawLibrary, identifier)
	}
	c := compiler{
		symbolPrefix:    formatLibraryPrefix(rawLibrary),
		decls:           r.DeclsWithDependencies(),
		library:         fidlgen.ParseLibraryName(r.Name),
		handleTypes:     make(map[fidlgen.HandleSubtype]struct{}),
		resultForStruct: make(map[fidlgen.EncodedCompoundIdentifier]*Result),
		resultForUnion:  make(map[fidlgen.EncodedCompoundIdentifier]*Result),
	}

	root.Library = library
	libraryReversed := make(fidlgen.LibraryIdentifier, len(library))
	for i, j := 0, len(library)-1; i < len(library); i, j = i+1, j-1 {
		libraryReversed[i] = library[j]
	}
	for i, identifier := range library {
		libraryReversed[len(libraryReversed)-i-1] = identifier
	}
	root.LibraryReversed = libraryReversed

	decls := make(map[fidlgen.EncodedCompoundIdentifier]Kinded)

	for _, v := range r.Bits {
		decls[v.Name] = c.compileBits(v)
	}

	for _, v := range r.Consts {
		decls[v.Name] = c.compileConst(v)
	}

	for _, v := range r.Enums {
		decls[v.Name] = c.compileEnum(v)
	}

	// Note: for Result calculation unions must be compiled before structs.
	for _, v := range r.Unions {
		decls[v.Name] = c.compileUnion(v)
	}

	for _, v := range r.Structs {
		// TODO(fxbug.dev/7704) remove once anonymous structs are supported
		if v.Anonymous {
			continue
		}
		decls[v.Name] = c.compileStruct(v)
	}

	for _, v := range r.Tables {
		decls[v.Name] = c.compileTable(v)
	}

	for _, v := range r.Protocols {
		decls[v.Name] = c.compileProtocol(v)
	}

	for _, v := range r.Services {
		decls[v.Name] = c.compileService(v)
	}

	for _, v := range r.DeclOrder {
		// We process only a subset of declarations mentioned in the declaration
		// order, ignore those we do not support.
		if d, known := decls[v]; known {
			root.Decls = append(root.Decls, d)
		}
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to include our own header.
			continue
		}
		libraryIdent := fidlgen.ParseLibraryName(l.Name)
		root.Headers = append(root.Headers, formatLibraryPath(libraryIdent))
		root.FuzzerHeaders = append(root.FuzzerHeaders, formatLibraryPath(libraryIdent))
	}

	// zx::channel is always referenced by the protocols in llcpp bindings API
	if len(r.Protocols) > 0 {
		c.handleTypes["channel"] = struct{}{}
	}

	// find all unique handle types referenced by the library
	var handleTypes []string
	for k := range c.handleTypes {
		handleTypes = append(handleTypes, string(k))
	}
	sort.Sort(sort.StringSlice(handleTypes))
	root.HandleTypes = handleTypes

	return root
}

func CompileHL(r fidlgen.Root) Root {
	return compile(r.ForBindings("hlcpp"))
}

func CompileLL(r fidlgen.Root) Root {
	return compile(r.ForBindings("llcpp"))
}

func CompileLibFuzzer(r fidlgen.Root) Root {
	return compile(r.ForBindings("libfuzzer"))
}
