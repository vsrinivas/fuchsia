// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rustgen

import (
	"log"
	"sort"
	"strconv"
	"strings"

	"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
)

// Holds the context of the traversal through the FidlFile object.
type Context struct {
	// We need access to the file graph to look at resolved types.
	FileGraph *fidl_files.FidlFileGraph
	// We need to know what file we are in so that we can handle imports.
	File *fidl_files.FidlFile
	// A map of file names to the names map
	RustNames *map[string]*Names
	// A map from filename to GN target
	Map *map[string]string
	// The root of the source
	SrcRootPath string
}

// Describes a field (such as a struct or a union member) to be printed.
// Used inside StructTemplate and UnionTemplate.
type FieldTemplate struct {
	Type string
	Name string
	// The minimum version necessary for the container to have this field.
	MinVersion uint32
	Offset     uint32
	IsUnion    bool
	IsNullable bool
}

// Describes a struct version with a version and a size.
type StructVersion struct {
	Version uint32
	Size    uint32
}
type StructVersions []StructVersion

// Describes the entirety of a struct to be printed.
type StructTemplate struct {
	// Generated Rust name of the struct.
	Name string
	// The size of the most recent version of the struct we know.
	Size uint32
	// The most recent version of the struct we know.
	Version uint32
	// All versions of this struct, including their sizes.
	Versions StructVersions
	// |fields| is in packing order, ready to be printed.
	Fields []FieldTemplate
	// Constants defined within this struct, in declaration order.
	Constants []ConstantTemplate
	// Enums defined within this struct, in declaration order.
	Enums []EnumTemplate
}

type UnionTemplate struct {
	// Generated Rust name of the union.
	Name string
	// just the field names, in tag order.
	Fields []FieldTemplate
	// the tags enum generated from the fields above.
	TagsEnum EnumTemplate
}

type EnumValueTemplate struct {
	// Generated Rust name of the enum name.
	Name string
	// Integer value of the enum; if the enum value was defined in the fidl IDL
	// as a reference, the value generated here will be the reduced int value.
	Value int64
}

type EnumTemplate struct {
	Name string
	// In declaration order.
	Values []EnumValueTemplate
	Signed bool
}

type EndpointTemplate struct {
	Name      string
	Interface string
}

type InterfaceMessageTemplate struct {
	Name    string
	RawName string
	TyName  string
	// This is the ordinal name (denoted by '@x' in the IDL).
	MessageOrdinal uint32
	MinVersion     uint32
	RequestStruct  StructTemplate
	// ResponseStruct.Name == "" if response not defined.
	ResponseStruct StructTemplate
}

type InterfaceTemplate struct {
	Name        string
	ServiceName string
	Client      EndpointTemplate
	Server      EndpointTemplate
	Version     uint32
	Messages    []InterfaceMessageTemplate
	Enums       []EnumTemplate
	Constants   []ConstantTemplate
}

// The type, name and initial value of the constant.
// Describes fidl constants declared top level, in structs, and in
// interfaces.
type ConstantTemplate struct {
	Type  string
	Name  string
	Value string
}

type SourceTemplate struct {
	Constants  []ConstantTemplate
	Structs    []StructTemplate
	Unions     []UnionTemplate
	Enums      []EnumTemplate
	Interfaces []InterfaceTemplate
}

func (c *Context) GetName(name string) string {
	if (*(*c.RustNames)[c.File.FileName])[name] == "" {
		log.Fatalf("Could not find name '%s' in file '%s'!", name, c.File.FileName)
	}
	return (*(*c.RustNames)[c.File.FileName])[name]
}

func (c *Context) GetNameFromFile(name string, file *fidl_files.FidlFile) string {
	if (*(*c.RustNames)[file.FileName])[name] == "" {
		log.Fatalf("Could not find name '%s' in file '%s'!", name, file.FileName)
	}
	return (*(*c.RustNames)[file.FileName])[name]
}

func (a StructVersions) Len() int           { return len(a) }
func (a StructVersions) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a StructVersions) Less(i, j int) bool { return a[i].Version < a[j].Version }

type SortableOrdinals []uint32

func (a SortableOrdinals) Len() int           { return len(a) }
func (a SortableOrdinals) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a SortableOrdinals) Less(i, j int) bool { return a[i] < a[j] }

// The following type allows us to sort fields in packing-order, which is defined
// by the byte+bit-offsets already provided to us by the fidl parser.
type FidlSortableStructFields []fidl_types.StructField

func (a FidlSortableStructFields) Len() int { return len(a) }
func (a FidlSortableStructFields) Swap(i, j int) {
	a[i], a[j] = a[j], a[i]
}
func (a FidlSortableStructFields) Less(i, j int) bool {
	if a[i].Offset == a[j].Offset {
		return a[i].Bit < a[j].Bit
	}
	return a[i].Offset < a[j].Offset
}

func NewEnumTemplate(context *Context, fidlEnum *fidl_types.FidlEnum) EnumTemplate {
	enumName := fidlToRustName(fidlEnum.DeclData, context, ident)

	var values []EnumValueTemplate
	for _, enumVal := range fidlEnum.Values {
		values = append(values, EnumValueTemplate{
			Name:  formatEnumValue(*enumVal.DeclData.ShortName),
			Value: int64(enumVal.IntValue),
		})
	}

	return EnumTemplate{
		Name:   enumName,
		Values: values,
		Signed: true,
	}
}

// Given a |fidl_types.Value|, returns the Rust mangled version of the value.
//  - if a literal, returns the Rust equivalent
//  - if a reference, returns the resolved, concrete value of the reference
//  (enum value or constant). This avoids any circular-dependency issues.
func resolveValue(context *Context, value fidl_types.Value) string {
	switch value.(type) {
	case *fidl_types.ValueLiteralValue:
		return fidlToRustLiteral(value.Interface().(fidl_types.LiteralValue))

	case *fidl_types.ValueConstantReference:
		const_key := value.Interface().(fidl_types.ConstantReference).ConstantKey
		if decl_const, exists := context.FileGraph.ResolvedConstants[const_key]; exists {
			constValue := decl_const.Value
			if decl_const.ResolvedConcreteValue != nil {
				constValue = decl_const.ResolvedConcreteValue
			}
			return resolveValue(context, constValue)
		}
		log.Fatal("Unresolved constant:", value)

	case *fidl_types.ValueEnumValueReference:
		enumRef := value.Interface().(fidl_types.EnumValueReference)
		if udt, exists := context.FileGraph.ResolvedTypes[enumRef.EnumTypeKey]; exists {
			enumType := udt.Interface().(fidl_types.FidlEnum)
			enumVal := enumType.Values[enumRef.EnumValueIndex]
			if enumVal.InitializerValue == nil {
				return strconv.FormatInt(int64(enumVal.IntValue), 10)
			}
			return resolveValue(context, enumVal.InitializerValue)
		}

	case *fidl_types.ValueBuiltinValue:
		return fidlToRustBuiltinValue(value.Interface().(fidl_types.BuiltinConstantValue))

	default:
		log.Fatal("Shouldn't be here. Unknown value type for:", value)
	}

	return ""
}

func NewConstantTemplate(context *Context, fidlConstant fidl_types.DeclaredConstant) ConstantTemplate {
	var type_text string
	switch fidlConstant.Type.(type) {
	// We can't type string constants as a 'String' since it
	// involves some setup and prevents immediate consumption.
	// &'static str should suffice.
	case *fidl_types.TypeStringType:
		type_text = "&'static str"
	default:
		type_text = fidlToRustType(fidlConstant.Type, context)
	}
	name := fidlToRustName(&fidlConstant.DeclData, context, formatConstName)
	val := resolveValue(context, fidlConstant.Value)
	if type_text == "f32" || type_text == "f64" {
		val = floatifyRustLiteral(val)
	}
	return ConstantTemplate{
		Type:  type_text,
		Name:  name,
		Value: val,
	}
}

func NewUnionTemplate(context *Context, fidlUnion *fidl_types.FidlUnion) UnionTemplate {
	union_name := fidlToRustName(fidlUnion.DeclData, context, ident)

	var fields []FieldTemplate
	var tag_enums []EnumValueTemplate
	for field_i, fidlField := range fidlUnion.Fields {
		field_name := *fidlField.DeclData.ShortName
		field_name = formatEnumValue(mangleReservedKeyword(field_name))
		field_type_text := fidlToRustType(fidlField.Type, context)
		is_union := fidlTypeIsUnion(fidlField.Type, context)
		is_nullable := strings.HasPrefix(field_type_text, "Option<")
		if fidlTypeNeedsBoxing(fidlField.Type, context) && !is_nullable {
			// Breaks recursion of recursive types.
			field_type_text = "Box<" + field_type_text + ">"
		}
		fields = append(fields, FieldTemplate{
			Type:       field_type_text,
			Name:       field_name,
			Offset:     0,
			IsUnion:    is_union,
			IsNullable: is_nullable,
		})
		tag_enums = append(tag_enums, EnumValueTemplate{
			Name:  field_name,
			Value: int64(field_i),
		})
	}

	return UnionTemplate{
		Name:   union_name,
		Fields: fields,
		TagsEnum: EnumTemplate{
			Name:   union_name + "_Tag",
			Values: tag_enums,
			Signed: false,
		},
	}
}

func generateContainedDeclarations(context *Context,
	decls *fidl_types.ContainedDeclarations) (constants []ConstantTemplate, enums []EnumTemplate) {
	if decls.Constants != nil {
		for _, fidlConstKey := range *decls.Constants {
			constants = append(constants,
				NewConstantTemplate(context, context.FileGraph.ResolvedConstants[fidlConstKey]))
		}
	}

	if decls.Enums != nil {
		for _, fidlEnumKey := range *decls.Enums {
			fidl_enum := context.FileGraph.ResolvedTypes[fidlEnumKey].Interface().(fidl_types.FidlEnum)
			enums = append(enums, NewEnumTemplate(context, &fidl_enum))
		}
	}
	return
}

func NewInterfaceTemplate(context *Context, fidlInterface *fidl_types.FidlInterface) InterfaceTemplate {
	interface_name := fidlToRustName(fidlInterface.DeclData, context, ident)
	var service_name string
	if fidlInterface.ServiceName != nil {
		service_name = *fidlInterface.ServiceName
	}
	client := EndpointTemplate{
		Name:      interface_name + "_Client",
		Interface: interface_name,
	}
	server := EndpointTemplate{
		Name:      interface_name + "_Server",
		Interface: interface_name,
	}

	// Generate templates for the containing constants and enums
	var constants []ConstantTemplate
	var enums []EnumTemplate
	if decls := fidlInterface.DeclData.ContainedDeclarations; decls != nil {
		constants, enums = generateContainedDeclarations(context, decls)
	}

	var msgs []InterfaceMessageTemplate
	ordinals := make([]uint32, len(fidlInterface.Methods))
	i := 0
	for k := range fidlInterface.Methods {
		ordinals[i] = k
		i++
	}
	sort.Sort(SortableOrdinals(ordinals))
	for _, ordinal := range ordinals {
		fidlMethod := fidlInterface.Methods[ordinal]
		shortName := *fidlMethod.DeclData.ShortName
		rawName := formatMethodName(shortName)
		msgName := mangleReservedKeyword(rawName)
		tyName := mangleReservedKeyword(formatEnumValue(shortName))

		req_struct := NewStructTemplate(context, &fidlMethod.Parameters)
		req_struct.Name = interface_name + "_" + req_struct.Name

		var resp_struct StructTemplate
		if fidlMethod.ResponseParams != nil {
			resp_struct = NewStructTemplate(context, fidlMethod.ResponseParams)
			resp_struct.Name = interface_name + "_" + resp_struct.Name
		}

		msgs = append(msgs, InterfaceMessageTemplate{
			Name:           msgName,
			TyName:         tyName,
			RawName:        rawName,
			MessageOrdinal: fidlMethod.Ordinal,
			MinVersion:     fidlMethod.MinVersion,
			RequestStruct:  req_struct,
			ResponseStruct: resp_struct,
		})
	}
	return InterfaceTemplate{
		Name:        interface_name,
		ServiceName: service_name,
		Client:      client,
		Server:      server,
		Version:     fidlInterface.CurrentVersion,
		Enums:       enums,
		Constants:   constants,
		Messages:    msgs,
	}
}

// TODO(vardhan): We can make this function, along with all other New*Template()
// functions, be a method for InterfaceTemplate so that we don't have to pass
// around |fileGraph| all over the place.
func NewStructTemplate(context *Context,
	fidlStruct *fidl_types.FidlStruct) StructTemplate {

	// Generate the printable fields.
	var fields []FieldTemplate
	for _, fidlField := range fidlStruct.Fields {
		field_name := *fidlField.DeclData.ShortName
		field_name = mangleReservedKeyword(field_name)
		field_type_text := fidlToRustType(fidlField.Type, context)
		offset := fidlField.Offset
		if fidlTypeIsBool(fidlField.Type) {
			offset = offset*8 + uint32(fidlField.Bit)
		}
		is_union := fidlTypeIsUnion(fidlField.Type, context)
		fields = append(fields, FieldTemplate{
			Type:       field_type_text,
			Name:       field_name,
			Offset:     offset,
			IsUnion:    is_union,
			MinVersion: fidlField.MinVersion,
		})
	}
	var size uint32
	var version uint32
	var versions StructVersions
	if fidlStruct.VersionInfo != nil {
		for _, structVersion := range *fidlStruct.VersionInfo {
			versions = append(versions, StructVersion{
				Version: structVersion.VersionNumber,
				Size:    structVersion.NumBytes,
			})
		}
		// Sort by verion number in increasing order.
		sort.Sort(versions)
		if len(versions) > 0 {
			current := versions[len(versions)-1]
			size = current.Size
			version = current.Version
		}
	}
	// If for some reason version information isn't available, try to
	// compute the size of the struct anyway.
	if size == 0 && len(fidlStruct.Fields) > 0 {
		// Sort fields by packing order (by offset,bit).
		sortedFields := fidlStruct.Fields
		sort.Sort(FidlSortableStructFields(sortedFields))
		final_field := sortedFields[len(sortedFields)-1]
		size = final_field.Offset
		if final_field.Bit > 0 {
			size += 1
		} else {
			size += fidlTypeSize(final_field.Type, context)
		}
		size = fidlAlignToBytes(size, 8)
	}

	// Generate templates for the containing constants and enums
	var constants []ConstantTemplate
	var enums []EnumTemplate
	if decls := fidlStruct.DeclData.ContainedDeclarations; decls != nil {
		constants, enums = generateContainedDeclarations(context, decls)
	}

	name := fidlToRustName(fidlStruct.DeclData, context, ident)
	return StructTemplate{
		Name:      name,
		Size:      size,
		Version:   version,
		Versions:  versions,
		Fields:    fields,
		Constants: constants,
		Enums:     enums,
	}
}

func NewSourceTemplate(context *Context) SourceTemplate {
	var constants []ConstantTemplate
	if context.File.DeclaredFidlObjects.TopLevelConstants != nil {
		for _, fidlConstKey := range *(context.File.DeclaredFidlObjects.TopLevelConstants) {
			fidlConst := context.FileGraph.ResolvedConstants[fidlConstKey]
			constants = append(constants, NewConstantTemplate(context, fidlConst))
		}
	}

	var enums []EnumTemplate
	if context.File.DeclaredFidlObjects.TopLevelEnums != nil {
		for _, fidlEnumKey := range *(context.File.DeclaredFidlObjects.TopLevelEnums) {
			fidlEnum := context.FileGraph.ResolvedTypes[fidlEnumKey].Interface().(fidl_types.FidlEnum)
			enums = append(enums, NewEnumTemplate(context, &fidlEnum))
		}
	}

	var unions []UnionTemplate
	if context.File.DeclaredFidlObjects.Unions != nil {
		for _, fidlUnionKey := range *(context.File.DeclaredFidlObjects.Unions) {
			fidlUnion := context.FileGraph.ResolvedTypes[fidlUnionKey].Interface().(fidl_types.FidlUnion)
			unions = append(unions, NewUnionTemplate(context, &fidlUnion))
		}
	}

	var structs []StructTemplate
	if context.File.DeclaredFidlObjects.Structs != nil {
		for _, fidlStructKey := range *(context.File.DeclaredFidlObjects.Structs) {
			fidlStruct := context.FileGraph.ResolvedTypes[fidlStructKey].Interface().(fidl_types.FidlStruct)
			structs = append(structs, NewStructTemplate(context, &fidlStruct))
		}
	}

	var interfaces []InterfaceTemplate
	if context.File.DeclaredFidlObjects.Interfaces != nil {
		for _, fidlIfaceKey := range *(context.File.DeclaredFidlObjects.Interfaces) {
			fidlIface := context.FileGraph.ResolvedTypes[fidlIfaceKey].Interface().(fidl_types.FidlInterface)
			interfaces = append(interfaces, NewInterfaceTemplate(context, &fidlIface))
		}
	}

	return SourceTemplate{
		Constants:  constants,
		Structs:    structs,
		Unions:     unions,
		Enums:      enums,
		Interfaces: interfaces,
	}
}
