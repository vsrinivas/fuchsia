// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rustgen

import (
	"log"
	"sort"
	"strconv"
	"strings"

	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

// Holds the context of the traversal through the MojomFile object.
type Context struct {
	// We need access to the file graph to look at resolved types.
	FileGraph *mojom_files.MojomFileGraph
	// We need to know what file we are in so that we can handle imports.
	File *mojom_files.MojomFile
	// A map of file names to the names map
	RustNames *map[string]*Names
}

// Describes a field (such as a struct or a union member) to be printed.
// Used inside StructTemplate and UnionTemplate.
type FieldTemplate struct {
	Type string
	Name string
	// The minimum version necessary for the container to have this field.
	MinVersion uint32
}

// Describes a struct version with a version and a size.
type StructVersion struct {
	Version  uint32
	Size uint32
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
	// Integer value of the enum; if the enum value was defined in the mojom IDL
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
	Name string
	Interface string
}

type InterfaceMessageTemplate struct {
	Name string
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
// Describes mojom constants declared top level, in structs, and in
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

func (c *Context) GetNameFromFile(name string, file *mojom_files.MojomFile) string {
	if (*(*c.RustNames)[file.FileName])[name] == "" {
		log.Fatalf("Could not find name '%s' in file '%s'!", name, file.FileName)
	}
	return (*(*c.RustNames)[file.FileName])[name]
}

func (a StructVersions) Len() int           { return len(a) }
func (a StructVersions) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a StructVersions) Less(i, j int) bool { return a[i].Version < a[j].Version }

// The follow type allows us to sort fields in packing-order, which is defined
// by the byte+bit-offsets already provided to us by the mojom parser.
type MojomSortableStructFields []mojom_types.StructField

func (a MojomSortableStructFields) Len() int { return len(a) }
func (a MojomSortableStructFields) Swap(i, j int) {
	a[i], a[j] = a[j], a[i]
}
func (a MojomSortableStructFields) Less(i, j int) bool {
	if a[i].Offset == a[j].Offset {
		return a[i].Bit < a[j].Bit
	}
	return a[i].Offset < a[j].Offset
}

func NewEnumTemplate(context *Context, mojomEnum *mojom_types.MojomEnum) EnumTemplate {
	enumName := mojomToRustName(mojomEnum.DeclData, context)

	var values []EnumValueTemplate
	for _, enumVal := range mojomEnum.Values {
		values = append(values, EnumValueTemplate{
			Name:  *enumVal.DeclData.ShortName,
			Value: int64(enumVal.IntValue),
		})
	}

	return EnumTemplate{
		Name:   enumName,
		Values: values,
		Signed: true,
	}
}

// Given a |mojom_types.Value|, returns the Rust mangled version of the value.
//  - if a literal, returns the Rust equivalent
//  - if a reference, returns the resolved, concrete value of the reference
//  (enum value or constant). This avoids any circular-dependency issues.
func resolveValue(context *Context, value mojom_types.Value) string {
	switch value.(type) {
	case *mojom_types.ValueLiteralValue:
		return mojomToRustLiteral(value.Interface().(mojom_types.LiteralValue))

	case *mojom_types.ValueConstantReference:
		const_key := value.Interface().(mojom_types.ConstantReference).ConstantKey
		if decl_const, exists := context.FileGraph.ResolvedConstants[const_key]; exists {
			constValue := decl_const.Value
			if decl_const.ResolvedConcreteValue != nil {
				constValue = decl_const.ResolvedConcreteValue
			}
			return resolveValue(context, constValue)
		}
		log.Fatal("Unresolved constant:", value)

	case *mojom_types.ValueEnumValueReference:
		enumRef := value.Interface().(mojom_types.EnumValueReference)
		if udt, exists := context.FileGraph.ResolvedTypes[enumRef.EnumTypeKey]; exists {
			enumType := udt.Interface().(mojom_types.MojomEnum)
			enumVal := enumType.Values[enumRef.EnumValueIndex]
			if enumVal.InitializerValue == nil {
				return strconv.FormatInt(int64(enumVal.IntValue), 10)
			}
			return resolveValue(context, enumVal.InitializerValue)
		}

	case *mojom_types.ValueBuiltinValue:
		return mojomToRustBuiltinValue(value.Interface().(mojom_types.BuiltinConstantValue))

	default:
		log.Fatal("Shouldn't be here. Unknown value type for:", value)
	}

	return ""
}

func NewConstantTemplate(context *Context, mojomConstant mojom_types.DeclaredConstant) ConstantTemplate {
	var type_text string
	switch mojomConstant.Type.(type) {
	// We can't type string constants as a 'String' since it
	// involves some setup and prevents immediate consumption.
	// &'static str should suffice.
	case *mojom_types.TypeStringType:
		type_text = "&'static str"
	default:
		type_text = mojomToRustType(mojomConstant.Type, context)
	}
	// Rust constants are always uppercase
	name := strings.ToUpper(mojomToRustName(&mojomConstant.DeclData, context))
	if isReservedKeyword(name) {
		log.Fatalf("Generated constant name '%s' is a reserved Rust keyword", name)
	}
	val := resolveValue(context, mojomConstant.Value)
	return ConstantTemplate{
		Type:  type_text,
		Name:  name,
		Value: val,
	}
}

func NewUnionTemplate(context *Context, mojomUnion *mojom_types.MojomUnion) UnionTemplate {
	union_name := mojomToRustName(mojomUnion.DeclData, context)

	// Get all variant names and mangle them as needed if they're keywords.
	names := make(Names)
	for _, mojomField := range mojomUnion.Fields {
		field_name := *mojomField.DeclData.ShortName
		names[field_name] = field_name
	}
	names.MangleKeywords()

	var fields []FieldTemplate
	var tag_enums []EnumValueTemplate
	for field_i, mojomField := range mojomUnion.Fields {
		field_name := *mojomField.DeclData.ShortName
		field_name = names[field_name]
		field_type_text := mojomToRustType(mojomField.Type, context)
		fields = append(fields, FieldTemplate{
			Type: field_type_text,
			Name: field_name,
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
			Name:   union_name + "Tag",
			Values: tag_enums,
			Signed: false,
		},
	}
}

func generateContainedDeclarations(context *Context,
	decls *mojom_types.ContainedDeclarations) (constants []ConstantTemplate, enums []EnumTemplate) {
	if decls.Constants != nil {
		for _, mojomConstKey := range *decls.Constants {
			constants = append(constants,
				NewConstantTemplate(context, context.FileGraph.ResolvedConstants[mojomConstKey]))
		}
	}

	if decls.Enums != nil {
		for _, mojomEnumKey := range *decls.Enums {
			mojom_enum := context.FileGraph.ResolvedTypes[mojomEnumKey].Interface().(mojom_types.MojomEnum)
			enums = append(enums, NewEnumTemplate(context, &mojom_enum))
		}
	}
	return
}

func NewInterfaceTemplate(context *Context, mojomInterface *mojom_types.MojomInterface) InterfaceTemplate {
	interface_name := mojomToRustName(mojomInterface.DeclData, context)
	var service_name string
	if mojomInterface.ServiceName != nil {
		service_name = *mojomInterface.ServiceName
	}
	client := EndpointTemplate{
		Name: interface_name + "Client",
		Interface: interface_name,
	}
	server := EndpointTemplate{
		Name: interface_name + "Server",
		Interface: interface_name,
	}

	// Generate templates for the containing constants and enums
	var constants []ConstantTemplate
	var enums []EnumTemplate
	if decls := mojomInterface.DeclData.ContainedDeclarations; decls != nil {
		constants, enums = generateContainedDeclarations(context, decls)
	}

	var msgs []InterfaceMessageTemplate
	for _, mojomMethod := range mojomInterface.Methods {
		msg_name := assertNotReservedKeyword(interface_name + *mojomMethod.DeclData.ShortName)
		req_struct := NewStructTemplate(context, &mojomMethod.Parameters)
		req_struct.Name = interface_name + req_struct.Name

		var resp_struct StructTemplate
		if mojomMethod.ResponseParams != nil {
			resp_struct = NewStructTemplate(context, mojomMethod.ResponseParams)
			resp_struct.Name = interface_name + resp_struct.Name
		}

		msgs = append(msgs, InterfaceMessageTemplate{
			Name:           msg_name,
			MessageOrdinal: mojomMethod.Ordinal,
			MinVersion:     mojomMethod.MinVersion,
			RequestStruct:  req_struct,
			ResponseStruct: resp_struct,
		})
	}
	return InterfaceTemplate{
		Name:        interface_name,
		ServiceName: service_name,
		Client:	     client,
		Server:      server,
		Version:     mojomInterface.CurrentVersion,
		Enums:       enums,
		Constants:   constants,
		Messages:    msgs,
	}
}

// TODO(vardhan): We can make this function, along with all other New*Template()
// functions, be a method for InterfaceTemplate so that we don't have to pass
// around |fileGraph| all over the place.
func NewStructTemplate(context *Context,
	mojomStruct *mojom_types.MojomStruct) StructTemplate {

	// Sort fields by packing order (by offset,bit).
	sortedFields := mojomStruct.Fields
	sort.Sort(MojomSortableStructFields(sortedFields))

	// Get all field names and mangle them appropriately
	names := make(Names)
	for _, mojomField := range sortedFields {
		field_name := *mojomField.DeclData.ShortName
		names[field_name] = field_name
	}
	names.MangleKeywords()

	// Generate the printable fields.
	var fields []FieldTemplate
	for _, mojomField := range sortedFields {
		field_name := *mojomField.DeclData.ShortName
		field_name = names[field_name]
		field_type_text := mojomToRustType(mojomField.Type, context)
		fields = append(fields, FieldTemplate{
			Type: field_type_text,
			Name: field_name,
			MinVersion: mojomField.MinVersion,
		})
	}
	var size uint32
	var version uint32
	var versions StructVersions
	if mojomStruct.VersionInfo != nil {
		for _, structVersion := range *mojomStruct.VersionInfo {
			versions = append(versions, StructVersion{
				Version:  structVersion.VersionNumber,
				Size: structVersion.NumBytes,
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
	if size == 0 && len(sortedFields) > 0 {
		final_field := sortedFields[len(sortedFields)-1]
		size = final_field.Offset
		if final_field.Bit > 0 {
			size += 1
		} else {
			size += mojomTypeSize(final_field.Type, context)
		}
		size = mojomAlignToBytes(size, 8)
	}

	// Generate templates for the containing constants and enums
	var constants []ConstantTemplate
	var enums []EnumTemplate
	if decls := mojomStruct.DeclData.ContainedDeclarations; decls != nil {
		constants, enums = generateContainedDeclarations(context, decls)
	}

	name := mojomToRustName(mojomStruct.DeclData, context)
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

func NewSourceTemplate(context *Context, srcRootPath string) SourceTemplate {
	var constants []ConstantTemplate
	if context.File.DeclaredMojomObjects.TopLevelConstants != nil {
		for _, mojomConstKey := range *(context.File.DeclaredMojomObjects.TopLevelConstants) {
			mojomConst := context.FileGraph.ResolvedConstants[mojomConstKey]
			constants = append(constants, NewConstantTemplate(context, mojomConst))
		}
	}

	var enums []EnumTemplate
	if context.File.DeclaredMojomObjects.TopLevelEnums != nil {
		for _, mojomEnumKey := range *(context.File.DeclaredMojomObjects.TopLevelEnums) {
			mojomEnum := context.FileGraph.ResolvedTypes[mojomEnumKey].Interface().(mojom_types.MojomEnum)
			enums = append(enums, NewEnumTemplate(context, &mojomEnum))
		}
	}

	var unions []UnionTemplate
	if context.File.DeclaredMojomObjects.Unions != nil {
		for _, mojomUnionKey := range *(context.File.DeclaredMojomObjects.Unions) {
			mojomUnion := context.FileGraph.ResolvedTypes[mojomUnionKey].Interface().(mojom_types.MojomUnion)
			unions = append(unions, NewUnionTemplate(context, &mojomUnion))
		}
	}

	var structs []StructTemplate
	if context.File.DeclaredMojomObjects.Structs != nil {
		for _, mojomStructKey := range *(context.File.DeclaredMojomObjects.Structs) {
			mojomStruct := context.FileGraph.ResolvedTypes[mojomStructKey].Interface().(mojom_types.MojomStruct)
			structs = append(structs, NewStructTemplate(context, &mojomStruct))
		}
	}

	var interfaces []InterfaceTemplate
	if context.File.DeclaredMojomObjects.Interfaces != nil {
		for _, mojomIfaceKey := range *(context.File.DeclaredMojomObjects.Interfaces) {
			mojomIface := context.FileGraph.ResolvedTypes[mojomIfaceKey].Interface().(mojom_types.MojomInterface)
			interfaces = append(interfaces, NewInterfaceTemplate(context, &mojomIface))
		}
	}

	return SourceTemplate{
		Constants:   constants,
		Structs:     structs,
		Unions:      unions,
		Enums:       enums,
		Interfaces:  interfaces,
	}
}

