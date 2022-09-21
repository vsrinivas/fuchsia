// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"bytes"
	"fmt"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type Attributes struct {
	fidlgen.Attributes
}

// Docs returns C++ documentation comments.
func (a Attributes) Docs() string {
	var buf bytes.Buffer
	for _, c := range a.DocComments() {
		buf.WriteString("\n///")
		buf.WriteString(c)
	}
	return buf.String()
}

type TypeShape struct {
	fidlgen.TypeShape
}

func (ts TypeShape) MaxTotalSize() int {
	return fidlAlign(ts.TypeShape.InlineSize) + fidlAlign(ts.TypeShape.MaxOutOfLine)
}

func (ts TypeShape) HasPointer() bool {
	return ts.TypeShape.Depth > 0
}

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
	nameVariants

	// Defines what operation we should use to pass a value without a move (LLCPP). It also
	// defines the way we should initialize a field.
	WireFamily familyKind

	// NeedsDtor indicates whether this type needs to be destructed explicitly
	// or not.
	NeedsDtor bool

	Kind typeKind

	IsResource bool
	Nullable   bool

	DeclarationName fidlgen.EncodedCompoundIdentifier

	// Set iff IsArray || IsVector
	ElementType *Type
	// Valid iff IsArray
	ElementCount int

	InlineInEnvelope bool

	// Used for type validation.
	NaturalFieldConstraint string
	WireFieldConstraint    string
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
		return fmt.Sprintf("%s(%s)", n, n)
	case FamilyKinds.Vector:
		return fmt.Sprintf("%s(%s)", n, n)
	default:
		panic(fmt.Sprintf("Unknown wire family kind %v", t.WireFamily))
	}
}

type namingContextKey = string

// toKey converts a naming context into a value that can be used as a map key
// (i.e. with equality), such that no different naming contexts produce the same
// key. A naming context is a stack of strings identifying the naming scopes
// that something is defined in; see naming-context in the IR and
// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0050_syntax_revamp?hl=en#layout-naming-contexts
// for details.
func toKey(idents []string) namingContextKey {
	// relies on the fact that '$' is not a valid part of an identifier
	return strings.Join(idents, "$")
}

// ScopedLayout represents the definition of a scoped name for an anonymous
// layout. It consists of the scoped name (defined within the parent layout),
// and the flattened name (defined at the top level)
type ScopedLayout struct {
	scopedName    stringNamePart
	flattenedName nameVariants
}

func (s ScopedLayout) ScopedName() string {
	return s.scopedName.String()
}

func (s ScopedLayout) FlattenedName() string {
	return s.flattenedName.NoLeading()
}

// A Member is a named member of a container (e.g.
// request/response/regular domain object).
type Member interface {
	NameAndType() (string, Type)
}

type Root struct {
	HandleTypes              []string
	Library                  fidlgen.LibraryIdentifier
	Decls                    []Kinded
	Dependencies             []fidlgen.LibraryIdentifier
	ContainsDriverReferences bool
}

func (r *Root) declsOfKind(kind declKind) []Kinded {
	ds := []Kinded{}
	for _, d := range r.Decls {
		if d.Kind() == kind {
			ds = append(ds, d)
		}
	}
	return ds
}

func (r *Root) Bits() []Kinded {
	return r.declsOfKind(Kinds.Bits)
}

func (r *Root) Consts() []Kinded {
	return r.declsOfKind(Kinds.Const)
}

func (r *Root) Enums() []Kinded {
	return r.declsOfKind(Kinds.Enum)
}

func (r *Root) Protocols() []Kinded {
	return r.declsOfKind(Kinds.Protocol)
}

func (r *Root) Services() []Kinded {
	return r.declsOfKind(Kinds.Service)
}

func (r *Root) Structs() []Kinded {
	return r.declsOfKind(Kinds.Struct)
}

func (r *Root) Tables() []Kinded {
	return r.declsOfKind(Kinds.Table)
}

func (r *Root) Unions() []Kinded {
	return r.declsOfKind(Kinds.Union)
}

func (r *Root) ProtocolsForTransport() func(string) []*Protocol {
	return func(t string) []*Protocol {
		var ps []*Protocol
		for _, k := range r.Protocols() {
			p := k.(*Protocol)
			_, ok := p.Transports()[t]
			if ok {
				ps = append(ps, p)
			}
		}
		return ps
	}
}

// ServicesForTransport returns services containing exclusively member
// protocols defined over the specified transport.
func (r *Root) ServicesForTransport() func(string) []*Service {
	return func(t string) []*Service {
		var ss []*Service
		for _, k := range r.Services() {
			s := k.(*Service)
			allOk := true
			var associatedTransport string
			for _, m := range s.Members {
				if m.ProtocolTransport.Name != t {
					allOk = false
				}
				if associatedTransport == "" {
					associatedTransport = m.ProtocolTransport.Name
				}
				if associatedTransport != m.ProtocolTransport.Name {
					fidlgen.TemplateFatalf("Mismatched transports: %s, %s. See fxbug.dev/106184",
						associatedTransport, m.ProtocolTransport.Name)
				}
			}
			if allOk {
				ss = append(ss, s)
			}
		}
		return ss
	}
}

func (r *Root) LegacyIncludeDir() string {
	return formatLibraryLegacyPath(r.Library) + "/cpp"
}

func (r *Root) UnifiedIncludeDir() string {
	return formatLibraryUnifiedPath(r.Library) + "/cpp"
}

// SingleComponentLibraryName returns if the FIDL library name only consists of
// a single identifier (e.g. "library foo;"). This is significant because the
// unified namespace and the natural namespace are identical when the library
// only has one component.
func (r *Root) SingleComponentLibraryName() bool {
	return len(r.Library) == 1
}

// Namespace returns the C++ namespace for generated protocol types this FIDL
// library.
func (r *Root) Namespace() namespace {
	switch currentVariant {
	case noVariant:
		fidlgen.TemplateFatalf("Called Root.Namespace() when currentVariant isn't set.\n")
	case hlcppVariant:
		return hlcppNamespace(r.Library)
	case unifiedVariant, wireVariant:
		return unifiedNamespace(r.Library)
	}
	panic("not reached")
}

// Result holds information about error results on methods.
type Result struct {
	ResultDecl        nameVariants
	ValueDecl         name
	ValueParameters   []Parameter
	ValueTypeDecl     nameVariants
	ValueTupleDecl    name
	Value             Type
	HasError          bool
	ErrorDecl         nameVariants
	Error             Type
	HasTransportError bool
	valueTypeIsStruct bool
}

func (r Result) ValueArity() int {
	return len(r.ValueParameters)
}

// BuildPayload renders a "destructured" access to all of the
// parameters described by a layout. For flattened parameter lists, this may
// look like (assuming the supplied `varName` is "_response"):
//
//	MyLayoutArgType _response = MyLayoutArgType{
//	  .a = std::move(a),
//	  .b = std::move(b),
//	};
//
// Unflattened layouts are always rendered as:
//
//	MyLayoutArgType _response = std::move(my_layout_arg);
func (r Result) BuildPayload(varName string) string {
	if len(r.ValueParameters) == 0 {
		return ""
	}
	out := fmt.Sprintf("%s %s = ", r.ValueTypeDecl, varName)
	if !r.valueTypeIsStruct && currentVariant != unifiedVariant {
		return out + fmt.Sprintf("std::move(%s);\n", r.Value.Name())
	}

	out += fmt.Sprintf("%s {\n", r.ValueTypeDecl)
	for _, v := range r.ValueParameters {
		out += fmt.Sprintf("  .%s = std::move(%s),\n", v.Name(), v.Name())
	}
	out += "};\n"
	return out
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
func NameVariantsForPrimitive(val fidlgen.PrimitiveSubtype) nameVariants {
	if t, ok := primitiveTypes[val]; ok {
		return primitiveNameVariants(t)
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
			parts = append(parts, changeIfReserved(string(part), nsComponentContext))
		} else {
			parts = append(parts, string(part))
		}
	}
	return parts
}

func formatLibraryPrefix(library fidlgen.LibraryIdentifier) string {
	return formatLibrary(library, "_", keepPartIfReserved)
}

func formatLibraryLegacyPath(library fidlgen.LibraryIdentifier) string {
	return formatLibrary(library, "/", keepPartIfReserved)

}
func formatLibraryUnifiedPath(library fidlgen.LibraryIdentifier) string {
	return fmt.Sprintf("fidl/%s", formatLibrary(library, ".", keepPartIfReserved))

}

func formatLibraryPath(library fidlgen.LibraryIdentifier) string {
	if currentVariant == wireVariant {
		return formatLibraryUnifiedPath(library)
	}
	return formatLibraryLegacyPath(library)
}

func codingTableName(ident fidlgen.EncodedCompoundIdentifier) string {
	ci := ident.Parse()
	return formatLibrary(ci.Library, "_", keepPartIfReserved) + "_" + string(ci.Name) + string(ci.Member)
}

type compiler struct {
	symbolPrefix       string
	decls              fidlgen.DeclInfoMap
	library            fidlgen.LibraryIdentifier
	handleTypes        map[fidlgen.HandleSubtype]struct{}
	resultForUnion     map[fidlgen.EncodedCompoundIdentifier]*Result
	messageBodyStructs map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct
	messageBodyTypes   fidlgen.EncodedCompoundIdentifierSet
	// anonymousChildren maps a layout (defined by its naming context key) to the
	// anonymous layouts defined directly within that layout. We opt to flatten
	// the naming context and use a map rather than a trie like structure for
	// simplicity.
	anonymousChildren        map[namingContextKey][]ScopedLayout
	containsDriverReferences bool
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

func (c *compiler) compileNameVariants(eci fidlgen.EncodedCompoundIdentifier) nameVariants {
	ci := eci.Parse()

	if isZirconIdentifier(ci) {
		return commonNameVariants(zirconName(ci))
	}

	declInfo, ok := c.decls[ci.EncodeDecl()]
	if !ok {
		panic(fmt.Sprintf("unknown identifier: %v", eci))
	}
	ctx := declContext(declInfo.Type)
	name := ctx.transform(ci) // Note: does not handle ci.Member
	if len(ci.Member) > 0 {
		member := memberNameContext(declInfo.Type).transform(ci.Member)
		name = name.nestVariants(member)
	}

	return name
}

func (c *compiler) compileCodingTableType(eci fidlgen.EncodedCompoundIdentifier) string {
	return fmt.Sprintf("%s_%sTable", c.symbolPrefix, eci.Parse().Name)
}

func (c *compiler) compileType(val fidlgen.Type) Type {
	r := Type{}
	r.Nullable = val.Nullable
	r.InlineInEnvelope = val.TypeShapeV2.InlineSize <= 4
	switch val.Kind {
	case fidlgen.ArrayType:
		t := c.compileType(*val.ElementType)
		// Because the unified bindings alias types from the natural domain objects,
		// the name _transformation_ would be identical between natural and unified,
		// here and below. We reserve the flexibility to specify different names
		// in the future.
		r.nameVariants = nameVariants{
			HLCPP:   makeName("std::array").arrayTemplate(t.HLCPP, *val.ElementCount),
			Unified: makeName("std::array").arrayTemplate(t.Unified, *val.ElementCount),
			Wire:    makeName("fidl::Array").arrayTemplate(t.Wire, *val.ElementCount),
		}
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Array
		r.IsResource = t.IsResource
		r.ElementType = &t
		r.ElementCount = *val.ElementCount
		r.NaturalFieldConstraint = t.NaturalFieldConstraint
		r.WireFieldConstraint = t.WireFieldConstraint
	case fidlgen.VectorType:
		t := c.compileType(*val.ElementType)
		r.nameVariants.Unified = makeName("std::vector").template(t.Unified)
		r.nameVariants.Wire = makeName("fidl::VectorView").template(t.Wire)
		if val.Nullable {
			r.nameVariants.HLCPP = makeName("fidl::VectorPtr").template(t.HLCPP)
			r.nameVariants.Unified = makeName("std::optional").template(r.nameVariants.Unified)
		} else {
			r.nameVariants.HLCPP = makeName("std::vector").template(t.HLCPP)
		}
		r.WireFamily = FamilyKinds.Vector
		r.NeedsDtor = true
		r.Kind = TypeKinds.Vector
		r.IsResource = t.IsResource
		r.ElementType = &t
		if val.ElementCount != nil {
			r.NaturalFieldConstraint = fmt.Sprintf("fidl::internal::NaturalCodingConstraintVector<%s, %d>", t.NaturalFieldConstraint, *val.ElementCount)
			r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintVector<%s, %t, %d>", t.WireFieldConstraint, val.Nullable, *val.ElementCount)
		} else {
			r.NaturalFieldConstraint = fmt.Sprintf("fidl::internal::NaturalCodingConstraintVector<%s>", t.NaturalFieldConstraint)
			r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintVector<%s, %t>", t.WireFieldConstraint, val.Nullable)
		}
	case fidlgen.StringType:
		r.Unified = makeName("std::string")
		r.Wire = makeName("fidl::StringView")
		if val.Nullable {
			r.HLCPP = makeName("fidl::StringPtr")
			r.nameVariants.Unified = makeName("std::optional").template(r.nameVariants.Unified)
		} else {
			r.HLCPP = makeName("std::string")
		}
		r.WireFamily = FamilyKinds.String
		r.NeedsDtor = true
		r.Kind = TypeKinds.String
		if val.ElementCount != nil {
			r.NaturalFieldConstraint = fmt.Sprintf("fidl::internal::NaturalCodingConstraintString<%d>", *val.ElementCount)
			r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintString<%t, %d>", val.Nullable, *val.ElementCount)
		} else {
			r.NaturalFieldConstraint = "fidl::internal::NaturalCodingConstraintString<>"
			r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintString<%t>", val.Nullable)
		}
	case fidlgen.HandleType:
		c.handleTypes[val.HandleSubtype] = struct{}{}
		if val.ResourceIdentifier == "fdf/handle" {
			c.containsDriverReferences = true
		}
		r.nameVariants = nameVariantsForHandle(val.ResourceIdentifier, val.HandleSubtype)
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Handle
		r.IsResource = true

		subtype, ok := handleSubtypeConsts[val.HandleSubtype]
		if !ok {
			panic(fmt.Sprintf("unknown handle type for const: %v", val))
		}
		r.NaturalFieldConstraint = fmt.Sprintf("fidl::internal::NaturalCodingConstraintHandle<ZX_OBJ_TYPE_%s, 0x%x, %t>", subtype, val.HandleRights, val.Nullable)
		r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintHandle<ZX_OBJ_TYPE_%s, 0x%x, %t>", subtype, val.HandleRights, val.Nullable)
	case fidlgen.RequestType:
		p := c.compileNameVariants(val.RequestSubtype)
		if val.ProtocolTransport == "Driver" {
			c.containsDriverReferences = true
		}
		t, ok := transports[val.ProtocolTransport]
		if !ok {
			panic(fmt.Sprintf("unknown transport %q", val.ProtocolTransport))
		}
		r.nameVariants = nameVariants{
			HLCPP:   makeName("fidl::InterfaceRequest").template(p.HLCPP),
			Unified: makeName(t.Namespace + "::ServerEnd").template(p.Unified),
			Wire:    makeName(t.Namespace + "::ServerEnd").template(p.Wire),
		}
		r.WireFamily = FamilyKinds.Reference
		r.NeedsDtor = true
		r.Kind = TypeKinds.Request
		r.IsResource = true
		r.NaturalFieldConstraint = fmt.Sprintf("fidl::internal::NaturalCodingConstraintHandle<ZX_OBJ_TYPE_CHANNEL, ZX_DEFAULT_CHANNEL_RIGHTS, %t>", val.Nullable)
		r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintHandle<ZX_OBJ_TYPE_CHANNEL, ZX_DEFAULT_CHANNEL_RIGHTS, %t>", val.Nullable)
	case fidlgen.PrimitiveType:
		r.nameVariants = NameVariantsForPrimitive(val.PrimitiveSubtype)
		r.WireFamily = FamilyKinds.TrivialCopy
		r.Kind = TypeKinds.Primitive
		r.NaturalFieldConstraint = "fidl::internal::NaturalCodingConstraintEmpty"
		r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
	case fidlgen.InternalType:
		switch val.InternalSubtype {
		case fidlgen.TransportErr:
			hlcppVariant := makeName("fidl::TransportErr")
			newcppVariant := makeName("fidl::internal::TransportErr")
			r.nameVariants = nameVariants{
				HLCPP:   hlcppVariant,
				Unified: newcppVariant,
				Wire:    newcppVariant,
			}
			r.Kind = TypeKinds.Enum
			r.WireFamily = FamilyKinds.TrivialCopy
			r.NaturalFieldConstraint = "fidl::internal::NaturalCodingConstraintEmpty"
			r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
		}
	case fidlgen.IdentifierType:
		name := c.compileNameVariants(val.Identifier)
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		declType := declInfo.Type
		if declType == fidlgen.ProtocolDeclType {
			if val.ProtocolTransport == "Driver" {
				c.containsDriverReferences = true
			}
			t, ok := transports[val.ProtocolTransport]
			if !ok {
				panic(fmt.Sprintf("unknown transport %q", val.ProtocolTransport))
			}
			r.nameVariants = nameVariants{
				HLCPP:   makeName("fidl::InterfaceHandle").template(name.HLCPP),
				Unified: makeName(t.Namespace + "::ClientEnd").template(name.Unified),
				Wire:    makeName(t.Namespace + "::ClientEnd").template(name.Wire),
			}
			r.WireFamily = FamilyKinds.Reference
			r.NeedsDtor = true
			r.Kind = TypeKinds.Protocol
			r.IsResource = true
			r.NaturalFieldConstraint = fmt.Sprintf("fidl::internal::NaturalCodingConstraintHandle<ZX_OBJ_TYPE_CHANNEL, ZX_DEFAULT_CHANNEL_RIGHTS, %t>", val.Nullable)
			r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintHandle<ZX_OBJ_TYPE_CHANNEL, ZX_DEFAULT_CHANNEL_RIGHTS, %t>", val.Nullable)
		} else {
			switch declType {
			case fidlgen.BitsDeclType:
				r.Kind = TypeKinds.Bits
				r.WireFamily = FamilyKinds.TrivialCopy
				r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
			case fidlgen.EnumDeclType:
				r.Kind = TypeKinds.Enum
				r.WireFamily = FamilyKinds.TrivialCopy
				r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
			case fidlgen.ConstDeclType:
				r.Kind = TypeKinds.Const
				r.WireFamily = FamilyKinds.Reference
				r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
			case fidlgen.StructDeclType:
				r.Kind = TypeKinds.Struct
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.IsResource = declInfo.IsResourceType()
				r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
			case fidlgen.TableDeclType:
				r.Kind = TypeKinds.Table
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.IsResource = declInfo.IsResourceType()
				r.WireFieldConstraint = "fidl::internal::WireCodingConstraintEmpty"
			case fidlgen.UnionDeclType:
				r.Kind = TypeKinds.Union
				r.DeclarationName = val.Identifier
				r.WireFamily = FamilyKinds.Reference
				r.IsResource = declInfo.IsResourceType()
				r.WireFieldConstraint = fmt.Sprintf("fidl::internal::WireCodingConstraintUnion<%t>", val.Nullable)
			default:
				panic(fmt.Sprintf("unknown declaration type: %v", declType))
			}

			if val.Nullable {
				r.nameVariants.HLCPP = makeName("std::unique_ptr").template(name.HLCPP)
				r.nameVariants.Unified = makeName("std::unique_ptr").template(name.Unified)
				if declType == fidlgen.UnionDeclType {
					r.nameVariants.Wire = name.Wire
				} else {
					r.nameVariants.Wire = makeName("fidl::ObjectView").template(name.Wire)
				}
				r.NeedsDtor = true
			} else {
				r.nameVariants = name
				r.NeedsDtor = true
			}
			r.NaturalFieldConstraint = "fidl::internal::NaturalCodingConstraintEmpty"
		}
	default:
		panic(fmt.Sprintf("unknown type kind: %v", val.Kind))
	}
	return r
}

func (c *compiler) getAnonymousChildren(layout fidlgen.LayoutDecl) []ScopedLayout {
	return c.anonymousChildren[toKey(layout.GetNamingContext())]
}

// Payloader is an interface describing operations that can be done to a
// payloadable (struct, table, or union) layout.
type Payloader interface {
	// AsParameters renders the payloadable layout as a set of parameters, which
	// is useful for operations like generating user-facing method signatures. For
	// table and union payloads, this method always produces a single parameter
	// pointing to the table/union itself, aka the "unflattened" representation.
	// Conversely, in certain (but not all) C++ bindings, structs are "flattened"
	// into a parameter list, with one parameter per struct member.
	AsParameters(*Type, *HandleInformation) []Parameter
}

func compile(r fidlgen.Root) *Root {
	root := Root{
		Library: r.Name.Parse(),
	}

	c := compiler{
		symbolPrefix:       formatLibraryPrefix(root.Library),
		decls:              r.DeclInfo(),
		library:            root.Library,
		handleTypes:        make(map[fidlgen.HandleSubtype]struct{}),
		resultForUnion:     make(map[fidlgen.EncodedCompoundIdentifier]*Result),
		messageBodyStructs: make(map[fidlgen.EncodedCompoundIdentifier]fidlgen.Struct),
		messageBodyTypes:   r.GetMessageBodyTypeNames(),
		anonymousChildren:  make(map[namingContextKey][]ScopedLayout),
	}

	addAnonymousLayouts := func(layout fidlgen.LayoutDecl) {
		if !layout.GetNamingContext().IsAnonymous() {
			return
		}

		// given a naming context ["foo", "bar", "baz"], we mark that the layout
		// at context ["foo", "bar"] has a child "baz"
		key := toKey(layout.GetNamingContext()[:len(layout.GetNamingContext())-1])
		c.anonymousChildren[key] = append(c.anonymousChildren[key], ScopedLayout{
			// TODO(fxbug.dev/60240): change this when other bindings use name transforms
			scopedName:    stringNamePart(fidlgen.ToUpperCamelCase(layout.GetNamingContext()[len(layout.GetNamingContext())-1])),
			flattenedName: c.compileNameVariants(layout.GetName()),
		})
	}
	for _, v := range r.Bits {
		addAnonymousLayouts(v)
	}
	for _, v := range r.Enums {
		addAnonymousLayouts(v)
	}
	for _, v := range r.Unions {
		addAnonymousLayouts(v)
	}
	for _, v := range r.Tables {
		addAnonymousLayouts(v)
	}
	for _, v := range r.Structs {
		addAnonymousLayouts(v)
	}
	for _, v := range r.ExternalStructs {
		addAnonymousLayouts(v)
	}

	decls := make(map[fidlgen.EncodedCompoundIdentifier]Kinded)
	extDecls := make(map[fidlgen.EncodedCompoundIdentifier]Kinded)

	for _, v := range r.Bits {
		decls[v.Name] = c.compileBits(v)
	}

	for _, v := range r.Consts {
		decls[v.Name] = c.compileConst(v)
	}

	for _, v := range r.Enums {
		decls[v.Name] = c.compileEnum(v)
	}

	for _, v := range r.Tables {
		decls[v.Name] = c.compileTable(v)
	}

	// Note: for results calculation, we must first compile unions, and structs.
	for _, v := range r.Unions {
		decls[v.Name] = c.compileUnion(v)
	}

	for _, v := range r.Structs {
		if _, ok := c.messageBodyTypes[v.Name]; ok {
			c.messageBodyStructs[v.Name] = v
		}
		decls[v.Name] = c.compileStruct(v)
	}

	for _, v := range r.ExternalStructs {
		if _, ok := c.messageBodyTypes[v.Name]; ok {
			c.messageBodyStructs[v.Name] = v
		}
		extDecls[v.Name] = c.compileStruct(v)
	}

	// Since union and table payloads, unlike structs, always use a single
	// "payload" argument pointing to the underlying union/table type, we only
	// need to store the name and (optional) owning result type of the union,
	// rather than the entire, flattenable declaration with all of its members.
	for _, v := range r.Libraries {
		for name, decl := range v.Decls {
			if decl.Type == fidlgen.TableDeclType {
				extDecls[name] = &TableName{nameVariants: c.compileNameVariants(name)}
			} else if decl.Type == fidlgen.UnionDeclType {
				extDecls[name] = &UnionName{nameVariants: c.compileNameVariants(name)}
			}
		}
	}

	for _, v := range r.Protocols {
		for _, m := range v.Methods {
			if m.HasError || m.HasTransportError() {
				var p Payloader
				valueTypeDecl, ok := decls[m.ValueType.Identifier]
				if ok {
					p = valueTypeDecl.(Payloader)
				} else {
					// If we are unable to look up the layout, this implies that it is externally defined. In
					// this case, the IR exposes the declaration.
					valueTypeDecl, ok := extDecls[m.ValueType.Identifier]
					if ok {
						p = valueTypeDecl.(Payloader)
					} else {
						panic(fmt.Sprintf("message body value type %s not found", m.ValueType.Identifier))
					}
				}
				result := c.compileResult(p, &m)
				if ok {
					switch s := p.(type) {
					case *Struct:
						s.SetInResult(result)
					}
					if resultTypeDecl, ok := decls[m.ResultType.Identifier]; !ok {
						panic(fmt.Sprintf("success struct %s in library, but result union %s is not",
							m.ValueType.Identifier, m.ResultType.Identifier))
					} else {
						u := resultTypeDecl.(*Union)
						u.Result = result
					}
				}
			}
		}
	}

	for _, v := range r.Protocols {
		decls[v.Name] = c.compileProtocol(v)
	}

	for _, v := range r.Services {
		decls[v.Name] = c.compileService(v)
	}

	g := NewDeclDepGraph(r)
	for _, v := range g.SortedDecls() {
		// We process only a subset of declarations mentioned in the declaration
		// order, ignore those we do not support.
		if d, known := decls[v.GetName()]; known {
			root.Decls = append(root.Decls, d)
		}
	}

	for _, l := range r.Libraries {
		if l.Name == r.Name {
			// We don't need to include our own header.
			continue
		}
		if l.Name == "zx" {
			// Skip the zircon types library.
			continue
		}
		root.Dependencies = append(root.Dependencies, l.Name.Parse())
	}

	// zx::channel is always referenced by the protocols in llcpp bindings API
	if len(r.Protocols) > 0 {
		c.handleTypes["channel"] = struct{}{}
	}

	// find all unique handle types referenced by the library
	var handleTypes []string
	for k := range c.handleTypes {
		handleTypes = append(handleTypes, handleHeaderName(k))
	}
	sort.Sort(sort.StringSlice(handleTypes))
	root.HandleTypes = handleTypes

	root.ContainsDriverReferences = c.containsDriverReferences

	return &root
}

func compileFor(r fidlgen.Root, n string) *Root {
	return compile(r.ForBindings(n))
}
