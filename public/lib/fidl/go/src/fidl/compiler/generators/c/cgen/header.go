// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cgen

import (
	"fmt"
	"log"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

// Describes a field (such as a struct or a union member) to be printed.
// Used inside StructTemplate and UnionTemplate.
type FieldTemplate struct {
	// Text contains both type and field name. We don't separate this into two
	// fields, since a) there's no real need, b) bools have bit field specifiers.
	Text string
	// Optional comment.
	Comment string
}

// Describes the entirety of a struct to be printed.
type StructTemplate struct {
	// Generated C name of the struct.
	Name string
	// |fields| is in packing order, ready to be printed.
	Fields []FieldTemplate
	// Constants defined within this struct, in declaration order.
	Constants []ConstantTemplate
	// Enums defined within this struct, in declaration order.
	Enums []EnumTemplate
}

type UnionTemplate struct {
	// Generated C name of the union.
	Name string
	// just the field names, in tag order.
	Fields []FieldTemplate
	// the tags enum generated from the fields above.
	TagsEnum EnumTemplate
}

type EnumValueTemplate struct {
	// Generated C name of the enum name.
	Name string
	// Integer value of the enum; if the enum value was defined in the mojom IDL
	// as a reference, the value generated here will be the reduced int value.
	Value int64
}

type EnumTemplate struct {
	Name string
	// In declaration order.
	Values []EnumValueTemplate
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

// Print things in roughly this order:
// - Imports
// - Struct forward decl
// - Union forward decl
// - Constants
// - Enum typedefs + constants for all enums
// - Union defs
// - Struct defs
// - Interface request+response defs
// - Type tables
type HeaderTemplate struct {
	HeaderGuard string
	Imports     []string
	Constants   []ConstantTemplate
	Enums       []EnumTemplate
	Structs     []StructTemplate
	Unions      []UnionTemplate
	Interfaces  []InterfaceTemplate
	TypeTable   TypeTableTemplate
}

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

func NewEnumTemplate(fileGraph *mojom_files.MojomFileGraph, mojomEnum *mojom_types.MojomEnum) EnumTemplate {
	enumName := mojomToCName(*mojomEnum.DeclData.FullIdentifier)
	if isReservedKeyword(enumName) {
		log.Fatalf("Generated name %s is a reserved C keyword", enumName)
	}

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
	}
}

// Given a |mojom_types.Value|, returns the C mangled version of the value.
//  - if a literal, returns the C equivalent
//  - if a reference, returns the resolved, concrete value of the reference
//  (enum value or constant). This avoids any circular-dependency issues.
func resolveValue(fileGraph *mojom_files.MojomFileGraph, value mojom_types.Value) string {
	switch value.(type) {
	case *mojom_types.ValueLiteralValue:
		return mojomToCLiteral(value.Interface().(mojom_types.LiteralValue))

	case *mojom_types.ValueConstantReference:
		const_key := value.Interface().(mojom_types.ConstantReference).ConstantKey
		if decl_const, exists := fileGraph.ResolvedConstants[const_key]; exists {
			constValue := decl_const.Value
			if decl_const.ResolvedConcreteValue != nil {
				constValue = decl_const.ResolvedConcreteValue
			}
			return resolveValue(fileGraph, constValue)
		}
		log.Fatal("Unresolved constant:", value)

	case *mojom_types.ValueEnumValueReference:
		enumRef := value.Interface().(mojom_types.EnumValueReference)
		if udt, exists := fileGraph.ResolvedTypes[enumRef.EnumTypeKey]; exists {
			enumType := udt.Interface().(mojom_types.MojomEnum)
			enumVal := enumType.Values[enumRef.EnumValueIndex]
			if enumVal.InitializerValue == nil {
				return strconv.FormatInt(int64(enumVal.IntValue), 10)
			}
			return resolveValue(fileGraph, enumVal.InitializerValue)
		}

	case *mojom_types.ValueBuiltinValue:
		return mojomToCBuiltinValue(value.Interface().(mojom_types.BuiltinConstantValue))

	default:
		log.Fatal("Shouldn't be here. Unknown value type for:", value)
	}

	return ""
}

func NewConstantTemplate(fileGraph *mojom_files.MojomFileGraph, mojomConstant mojom_types.DeclaredConstant) ConstantTemplate {
	var type_text string
	switch mojomConstant.Type.(type) {
	// We can't type string constants as a 'union MojomStringHeaderPtr' since it
	// involves some setup and prevents immediate consumption. const char* should
	// suffice.
	case *mojom_types.TypeStringType:
		type_text = "char*"
	default:
		type_text = mojomToCType(mojomConstant.Type, fileGraph)
	}
	name := mojomToCName(*mojomConstant.DeclData.FullIdentifier)
	if isReservedKeyword(name) {
		log.Fatalf("Generated name %s is a reserved C keyword", name)
	}
	val := resolveValue(fileGraph, mojomConstant.Value)
	return ConstantTemplate{
		Type:  type_text,
		Name:  name,
		Value: val,
	}
}

func NewUnionTemplate(fileGraph *mojom_files.MojomFileGraph, mojomUnion *mojom_types.MojomUnion) UnionTemplate {
	union_name := mojomToCName(*mojomUnion.DeclData.FullIdentifier)
	if isReservedKeyword(union_name) {
		log.Fatalf("Generated name %s is a reserved C keyword", union_name)
	}
	var fields []FieldTemplate
	var tag_enums []EnumValueTemplate
	for field_i, mojomField := range mojomUnion.Fields {
		field_name := *mojomField.DeclData.ShortName
		var field_text string
		if mojomTypeIsBool(mojomField.Type) {
			field_text = fmt.Sprintf("bool %s : 1", field_name)
		} else {
			field_type_text := mojomToCType(mojomField.Type, fileGraph)
			if mojomTypeIsUnion(mojomField.Type, fileGraph) {
				// Unions within unions are pointers/references, not inlined.
				// This will transform: "struct MyUnion" -> "union MyUnionPtr"
				// TODO(vardhan): This feels hacky..
				field_type_text = strings.Replace(field_type_text, "struct ", "union ", 1)
				field_type_text += "Ptr"
			}
			field_text = fmt.Sprintf("%s f_%s", field_type_text, field_name)
		}
		fields = append(fields, FieldTemplate{
			Text: field_text,
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
		},
	}
}

func generateContainedDeclarations(fileGraph *mojom_files.MojomFileGraph,
	decls *mojom_types.ContainedDeclarations) (constants []ConstantTemplate, enums []EnumTemplate) {
	if decls.Constants != nil {
		for _, mojomConstKey := range *decls.Constants {
			constants = append(constants,
				NewConstantTemplate(fileGraph, fileGraph.ResolvedConstants[mojomConstKey]))
		}
	}

	if decls.Enums != nil {
		for _, mojomEnumKey := range *decls.Enums {
			mojom_enum := fileGraph.ResolvedTypes[mojomEnumKey].Interface().(mojom_types.MojomEnum)
			enums = append(enums, NewEnumTemplate(fileGraph, &mojom_enum))
		}
	}
	return
}

func NewInterfaceTemplate(fileGraph *mojom_files.MojomFileGraph, mojomInterface *mojom_types.MojomInterface) InterfaceTemplate {
	interface_name := mojomToCName(*mojomInterface.DeclData.FullIdentifier)
	var service_name string
	if mojomInterface.ServiceName != nil {
		service_name = *mojomInterface.ServiceName
	}

	// Generate templates for the containing constants and enums
	var constants []ConstantTemplate
	var enums []EnumTemplate
	if decls := mojomInterface.DeclData.ContainedDeclarations; decls != nil {
		constants, enums = generateContainedDeclarations(fileGraph, decls)
	}

	var msgs []InterfaceMessageTemplate
	for _, mojomMethod := range mojomInterface.Methods {
		req_struct := NewStructTemplate(fileGraph, &mojomMethod.Parameters)
		req_struct.Name = requestMethodToCName(mojomInterface, &mojomMethod.Parameters)

		var resp_struct StructTemplate
		if mojomMethod.ResponseParams != nil {
			resp_struct = NewStructTemplate(fileGraph, mojomMethod.ResponseParams)
			resp_struct.Name = responseMethodToCName(mojomInterface, mojomMethod.ResponseParams)
		}

		msgs = append(msgs, InterfaceMessageTemplate{
			Name:           *mojomMethod.DeclData.ShortName,
			MessageOrdinal: mojomMethod.Ordinal,
			MinVersion:     mojomMethod.MinVersion,
			RequestStruct:  req_struct,
			ResponseStruct: resp_struct,
		})
	}

	return InterfaceTemplate{
		Name:        interface_name,
		ServiceName: service_name,
		Version:     mojomInterface.CurrentVersion,
		Enums:       enums,
		Constants:   constants,
		Messages:    msgs,
	}
}

// TODO(vardhan): We can make this function, along with all other New*Template()
// functions, be a method for InterfaceTemplate so that we don't have to pass
// around |fileGraph| all over the place.
func NewStructTemplate(fileGraph *mojom_files.MojomFileGraph,
	mojomStruct *mojom_types.MojomStruct) StructTemplate {
	// Sort fields by packing order (by offset,bit).
	sortedFields := mojomStruct.Fields
	sort.Sort(MojomSortableStructFields(sortedFields))

	// Generate the printable fields.
	var fields []FieldTemplate
	for i, mojomField := range sortedFields {
		field_name := *mojomField.DeclData.ShortName
		if isReservedKeyword(field_name) {
			log.Fatalf("Generated field name %s is a reserved C keyword", field_name)
		}
		var field_text string
		if mojomTypeIsBool(mojomField.Type) {
			field_text = fmt.Sprintf("bool %s : 1", field_name)
		} else {
			field_text = fmt.Sprintf("%s %s",
				mojomToCType(mojomField.Type, fileGraph), field_name)
		}
		fields = append(fields, FieldTemplate{
			Text:    field_text,
			Comment: fmt.Sprintf("offset,bit = %d,%d", mojomField.Offset, mojomField.Bit),
		})

		// Compute the padding between this field and the next.
		if padding := getPaddingAfter(sortedFields, i, fileGraph); padding > 0 {
			fields = append(fields, FieldTemplate{
				Text:    fmt.Sprintf("uint8_t pad%d_[%d]", i, padding),
				Comment: "padding"})
		}
	}

	// Generate templates for the containing constants and enums
	var constants []ConstantTemplate
	var enums []EnumTemplate
	if decls := mojomStruct.DeclData.ContainedDeclarations; decls != nil {
		constants, enums = generateContainedDeclarations(fileGraph, decls)
	}

	// FullIdentifier may not exist if this struct is an interface method params struct.
	var name string
	if mojomStruct.DeclData.FullIdentifier != nil {
		name = mojomToCName(*mojomStruct.DeclData.FullIdentifier)
	} else {
		name = mojomToCName(*mojomStruct.DeclData.ShortName)
	}
	if isReservedKeyword(name) {
		log.Fatalf("Generated struct name %s is a reserved C keyword", name)
	}
	return StructTemplate{
		Name:      name,
		Fields:    fields,
		Constants: constants,
		Enums:     enums,
	}
}

func NewHeaderTemplate(fileGraph *mojom_files.MojomFileGraph, file *mojom_files.MojomFile, srcRootPath string) HeaderTemplate {
	var relGenPath string
	var err error
	if relGenPath, err = filepath.Rel(srcRootPath, file.FileName); err != nil {
		log.Fatal(err)
	}

	var imports []string
	if file.Imports != nil {
		for _, import_str := range *file.Imports {
			imports = append(imports, mojomToCFilePath(srcRootPath, import_str))
		}
	}

	var constants []ConstantTemplate
	if file.DeclaredMojomObjects.TopLevelConstants != nil {
		for _, mojomConstKey := range *(file.DeclaredMojomObjects.TopLevelConstants) {
			mojomConst := fileGraph.ResolvedConstants[mojomConstKey]
			constants = append(constants, NewConstantTemplate(fileGraph, mojomConst))
		}
	}

	var enums []EnumTemplate
	if file.DeclaredMojomObjects.TopLevelEnums != nil {
		for _, mojomEnumKey := range *(file.DeclaredMojomObjects.TopLevelEnums) {
			mojomEnum := fileGraph.ResolvedTypes[mojomEnumKey].Interface().(mojom_types.MojomEnum)
			enums = append(enums, NewEnumTemplate(fileGraph, &mojomEnum))
		}
	}

	var unions []UnionTemplate
	if file.DeclaredMojomObjects.Unions != nil {
		for _, mojomUnionKey := range *(file.DeclaredMojomObjects.Unions) {
			mojomUnion := fileGraph.ResolvedTypes[mojomUnionKey].Interface().(mojom_types.MojomUnion)
			unions = append(unions, NewUnionTemplate(fileGraph, &mojomUnion))
		}
	}

	var structs []StructTemplate
	if file.DeclaredMojomObjects.Structs != nil {
		for _, mojomStructKey := range *(file.DeclaredMojomObjects.Structs) {
			mojomStruct := fileGraph.ResolvedTypes[mojomStructKey].Interface().(mojom_types.MojomStruct)
			structs = append(structs, NewStructTemplate(fileGraph, &mojomStruct))
		}
	}

	var interfaces []InterfaceTemplate
	if file.DeclaredMojomObjects.Interfaces != nil {
		for _, mojomIfaceKey := range *(file.DeclaredMojomObjects.Interfaces) {
			mojomIface := fileGraph.ResolvedTypes[mojomIfaceKey].Interface().(mojom_types.MojomInterface)
			interfaces = append(interfaces, NewInterfaceTemplate(fileGraph, &mojomIface))
		}
	}

	return HeaderTemplate{
		HeaderGuard: toHeaderGuard(relGenPath),
		Imports:     imports,
		Constants:   constants,
		Structs:     structs,
		Unions:      unions,
		Enums:       enums,
		Interfaces:  interfaces,
		TypeTable:   NewTypeTableTemplate(fileGraph, file),
	}
}

func getPaddingAfter(fields []mojom_types.StructField, i int, fileGraph *mojom_files.MojomFileGraph) uint32 {
	// Must be a power of 2 for the remaining computation to work
	const kAlignment uint32 = 8

	// Calculate the remaining padding for the last field
	if i == len(fields)-1 {
		// m = (field offset + field size) % kAlignment
		m := (fields[i].Offset + mojomTypeByteSize(fields[i].Type, fileGraph)) & (kAlignment - 1)
		if m != 0 {
			return kAlignment - m
		}
		return 0
	}

	// (next element's offset)
	//  - (current element's offset + current element's size)
	diff := int64(fields[i+1].Offset) - (int64(fields[i].Offset) + int64(mojomTypeByteSize(fields[i].Type, fileGraph)))
	if diff <= 0 {
		return 0
	}
	return uint32(diff)
}
