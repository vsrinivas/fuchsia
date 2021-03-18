// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"sort"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// These are used in header/impl templates to select the correct type-specific template.
// Each individual type is embedded into the corresponding C++ IR struct. Omit those
// fields when initializing IRs.
type (
	bitsKind     struct{}
	constKind    struct{}
	enumKind     struct{}
	protocolKind struct{}
	serviceKind  struct{}
	structKind   struct{}
	tableKind    struct{}
	unionKind    struct{}
)

var Kinds = struct {
	Const    constKind
	Bits     bitsKind
	Enum     enumKind
	Protocol protocolKind
	Service  serviceKind
	Struct   structKind
	Table    tableKind
	Union    unionKind
}{}

// A Decl is any type with a .Kind field.
type Decl interface{}

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

// TypeKinds are the kinds of declarations (arrays, primitives, structs, ...).
var TypeKinds = namespacedEnum(typeKinds{}).(typeKinds)

type Type struct {
	TypeName

	WirePointer bool

	// Defines what operation we should use to pass a value without a move (LLCPP). It also
	// defines the way we should initialize a field.
	WireFamily familyKind

	// NeedsDtor indicates whether this type needs to be destructed explicitely
	// or not.
	NeedsDtor bool

	Kind typeKind

	IsResource bool

	DeclarationName fidl.EncodedCompoundIdentifier

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
	Library         fidl.LibraryIdentifier
	LibraryReversed fidl.LibraryIdentifier
	Decls           []Decl
}

// Holds information about error results on methods
type Result struct {
	ValueMembers    []Parameter
	ResultDecl      DeclName
	ErrorDecl       TypeName
	ValueDecl       TypeVariant
	ValueStructDecl TypeName
	ValueTupleDecl  TypeVariant
}

func (r Result) ValueArity() int {
	return len(r.ValueMembers)
}

var primitiveTypes = map[fidl.PrimitiveSubtype]string{
	fidl.Bool:    "bool",
	fidl.Int8:    "int8_t",
	fidl.Int16:   "int16_t",
	fidl.Int32:   "int32_t",
	fidl.Int64:   "int64_t",
	fidl.Uint8:   "uint8_t",
	fidl.Uint16:  "uint16_t",
	fidl.Uint32:  "uint32_t",
	fidl.Uint64:  "uint64_t",
	fidl.Float32: "float",
	fidl.Float64: "double",
}

// TypeNameForPrimitive returns the C++ name of a FIDL primitive type.
func TypeNameForPrimitive(val fidl.PrimitiveSubtype) TypeName {
	if t, ok := primitiveTypes[val]; ok {
		return PrimitiveTypeName(t)
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

type identifierTransform bool

const (
	keepPartIfReserved   identifierTransform = false
	changePartIfReserved identifierTransform = true
)

func libraryParts(library fidl.LibraryIdentifier, identifierTransform identifierTransform) []string {
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

func formatLibraryPrefix(library fidl.LibraryIdentifier) string {
	return formatLibrary(library, "_", keepPartIfReserved)
}

func formatLibraryPath(library fidl.LibraryIdentifier) string {
	return formatLibrary(library, "/", keepPartIfReserved)
}

type libraryNamespaceFunc func(fidl.LibraryIdentifier) Namespace

func codingTableName(ident fidl.EncodedCompoundIdentifier) string {
	ci := fidl.ParseCompoundIdentifier(ident)
	return formatLibrary(ci.Library, "_", keepPartIfReserved) + "_" + string(ci.Name) + string(ci.Member)
}

type compiler struct {
	symbolPrefix    string
	decls           fidl.DeclInfoMap
	library         fidl.LibraryIdentifier
	handleTypes     map[fidl.HandleSubtype]struct{}
	resultForStruct map[fidl.EncodedCompoundIdentifier]*Result
	resultForUnion  map[fidl.EncodedCompoundIdentifier]*Result
}

func (c *compiler) isInExternalLibrary(ci fidl.CompoundIdentifier) bool {
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

func (c *compiler) compileDeclName(eci fidl.EncodedCompoundIdentifier) DeclName {
	ci := fidl.ParseCompoundIdentifier(eci)
	if ci.Member != fidl.Identifier("") {
		panic(fmt.Sprintf("unexpected compound identifier with member: %v", eci))
	}
	name := changeIfReserved(ci.Name)
	declInfo, ok := c.decls[eci]
	if !ok {
		panic(fmt.Sprintf("unknown identifier: %v", eci))
	}
	declType := declInfo.Type
	switch declType {
	case fidl.ConstDeclType, fidl.BitsDeclType, fidl.EnumDeclType, fidl.StructDeclType, fidl.TableDeclType, fidl.UnionDeclType:
		return DeclName{
			Natural: NewDeclVariant(name, naturalNamespace(ci.Library)),
			Wire:    NewDeclVariant(name, wireNamespace(ci.Library)),
		}
	case fidl.ProtocolDeclType, fidl.ServiceDeclType:
		return DeclName{
			Natural: NewDeclVariant(name, naturalNamespace(ci.Library)),
			Wire:    NewDeclVariant(name, unifiedNamespace(ci.Library)),
		}
	}
	panic("Unknown decl type: " + string(declType))
}

func (c *compiler) compileCodingTableType(eci fidl.EncodedCompoundIdentifier) string {
	val := fidl.ParseCompoundIdentifier(eci)
	if c.isInExternalLibrary(val) {
		panic(fmt.Sprintf("can't create coding table type for external identifier: %v", val))
	}

	return fmt.Sprintf("%s_%sTable", c.symbolPrefix, val.Name)
}

func (c *compiler) compileType(val fidl.Type) Type {
	r := Type{}
	switch val.Kind {
	case fidl.ArrayType:
		t := c.compileType(*val.ElementType)
		r.TypeName = t.TypeName.WithArrayTemplates("::std::array", "::fidl::Array", *val.ElementCount)
		r.WirePointer = t.WirePointer
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Array
		r.IsResource = t.IsResource
		r.ElementType = &t
		r.ElementCount = *val.ElementCount
	case fidl.VectorType:
		t := c.compileType(*val.ElementType)
		r.TypeName = t.TypeName.WithTemplates(
			map[bool]string{true: "::fidl::VectorPtr", false: "::std::vector"}[val.Nullable],
			"::fidl::VectorView")
		r.WireFamily = FamilyKinds.Vector
		r.WirePointer = t.WirePointer
		r.NeedsDtor = true
		r.Kind = TypeKinds.Vector
		r.IsResource = t.IsResource
		r.ElementType = &t
	case fidl.StringType:
		r.Wire = TypeVariant("::fidl::StringView")
		r.WireFamily = FamilyKinds.String
		if val.Nullable {
			r.Natural = TypeVariant("::fidl::StringPtr")
		} else {
			r.Natural = TypeVariant("::std::string")
		}
		r.NeedsDtor = true
		r.Kind = TypeKinds.String
	case fidl.HandleType:
		c.handleTypes[val.HandleSubtype] = struct{}{}
		r.TypeName = TypeNameForHandle(val.HandleSubtype)
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Handle
		r.IsResource = true
	case fidl.RequestType:
		r.TypeName = c.compileDeclName(val.RequestSubtype).TypeName().WithTemplates("::fidl::InterfaceRequest", "::fidl::ServerEnd")
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Request
		r.IsResource = true
	case fidl.PrimitiveType:
		r.TypeName = TypeNameForPrimitive(val.PrimitiveSubtype)
		r.WireFamily = FamilyKinds.TrivialCopy
		r.Kind = TypeKinds.Primitive
	case fidl.IdentifierType:
		name := c.compileDeclName(val.Identifier).TypeName()
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		declType := declInfo.Type
		if declType == fidl.ProtocolDeclType {
			r.TypeName = name.WithTemplates("::fidl::InterfaceHandle", "::fidl::ClientEnd")
			r.WireFamily = FamilyKinds.Reference
			r.NeedsDtor = true
			r.Kind = TypeKinds.Protocol
			r.IsResource = true
		} else {
			switch declType {
			case fidl.BitsDeclType:
				r.Kind = TypeKinds.Bits
				r.WireFamily = FamilyKinds.TrivialCopy
			case fidl.EnumDeclType:
				r.Kind = TypeKinds.Enum
				r.WireFamily = FamilyKinds.TrivialCopy
			case fidl.ConstDeclType:
				r.Kind = TypeKinds.Const
				r.WireFamily = FamilyKinds.Reference
			case fidl.StructDeclType:
				r.Kind = TypeKinds.Struct
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.WirePointer = val.Nullable
				r.IsResource = declInfo.IsResourceType()
			case fidl.TableDeclType:
				r.Kind = TypeKinds.Table
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.WirePointer = val.Nullable
				r.IsResource = declInfo.IsResourceType()
			case fidl.UnionDeclType:
				r.Kind = TypeKinds.Union
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.IsResource = declInfo.IsResourceType()
			default:
				panic(fmt.Sprintf("unknown declaration type: %v", declType))
			}

			if val.Nullable {
				r.TypeName = name.MapNatural(func(n TypeVariant) TypeVariant {
					return n.WithTemplate("::std::unique_ptr")
				}).MapWire(func(n TypeVariant) TypeVariant {
					if declType == fidl.UnionDeclType {
						return n
					} else {
						return n.WithTemplate("::fidl::tracking_ptr")
					}
				})
				r.NeedsDtor = true
			} else {
				r.TypeName = name
				r.NeedsDtor = true
			}
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}
	return r
}

func compile(r fidl.Root) Root {
	root := Root{}
	library := make(fidl.LibraryIdentifier, 0)
	rawLibrary := make(fidl.LibraryIdentifier, 0)
	for _, identifier := range fidl.ParseLibraryName(r.Name) {
		safeName := changeIfReserved(identifier)
		library = append(library, fidl.Identifier(safeName))
		rawLibrary = append(rawLibrary, identifier)
	}
	c := compiler{
		symbolPrefix:    formatLibraryPrefix(rawLibrary),
		decls:           r.DeclsWithDependencies(),
		library:         fidl.ParseLibraryName(r.Name),
		handleTypes:     make(map[fidl.HandleSubtype]struct{}),
		resultForStruct: make(map[fidl.EncodedCompoundIdentifier]*Result),
		resultForUnion:  make(map[fidl.EncodedCompoundIdentifier]*Result),
	}

	root.Library = library
	libraryReversed := make(fidl.LibraryIdentifier, len(library))
	for i, j := 0, len(library)-1; i < len(library); i, j = i+1, j-1 {
		libraryReversed[i] = library[j]
	}
	for i, identifier := range library {
		libraryReversed[len(libraryReversed)-i-1] = identifier
	}
	root.LibraryReversed = libraryReversed

	decls := make(map[fidl.EncodedCompoundIdentifier]Decl)

	for _, v := range r.Bits {
		d := c.compileBits(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Consts {
		d := c.compileConst(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Enums {
		d := c.compileEnum(v)
		decls[v.Name] = &d
	}

	// Note: for Result calculation unions must be compiled before structs.
	for _, v := range r.Unions {
		d := c.compileUnion(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Structs {
		// TODO(fxbug.dev/7704) remove once anonymous structs are supported
		if v.Anonymous {
			continue
		}
		d := c.compileStruct(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Tables {
		d := c.compileTable(v)
		decls[v.Name] = &d
	}

	for _, v := range r.Protocols {
		d := c.compileProtocol(v)
		decls[v.Name] = d
	}

	for _, v := range r.Services {
		d := c.compileService(v)
		decls[v.Name] = &d
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
		libraryIdent := fidl.ParseLibraryName(l.Name)
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

func CompileHL(r fidl.Root) Root {
	return compile(r.ForBindings("hlcpp"))
}

func CompileLL(r fidl.Root) Root {
	return compile(r.ForBindings("llcpp"))
}

func CompileLibFuzzer(r fidl.Root) Root {
	return compile(r.ForBindings("libfuzzer"))
}
