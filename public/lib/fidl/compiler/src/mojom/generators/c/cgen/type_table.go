// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(vardhan): Occurrances of "type table" and "pointer table" should be
// "type descriptors".

package cgen

import (
	"fmt"
	"log"
	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
	"sort"
)

type StructPointerTableEntry struct {
	ElemTable  string
	Offset     uint32
	MinVersion uint32
	ElemType   string
	Nullable   bool
}

type UnionPointerTableEntry struct {
	ElemTable string
	Tag       uint32
	Nullable  bool
	ElemType  string
}

type ArrayPointerTableEntry struct {
	Name        string
	ElemTable   string
	NumElements uint32
	Nullable    bool
	ElemType    string
	ElemNumBits uint32
}

type StructVersion struct {
	Version  uint32
	NumBytes uint32
}
type StructVersions []StructVersion

type StructPointerTable struct {
	Name string
	// List of version -> struct sizes, ordered by increasing version.
	Versions StructVersions
	Entries  []StructPointerTableEntry
}
type UnionPointerTable struct {
	Name string
	// Entries only includes entries for pointer and handle types.
	Entries []UnionPointerTableEntry
	// The number of fields in the union (this can be used to determine valid tag
	// numbers).
	NumFields uint32
}

type TypeTableTemplate struct {
	Structs []StructPointerTable
	Unions  []UnionPointerTable
	Arrays  []ArrayPointerTableEntry

	// These are used for declarations in header files.
	PublicUnionNames  []string
	PublicStructNames []string

	// This counter is used to name recursive types (array of arrays, maps, etc.).
	counter uint32

	// Used to look up user-defined references.
	fileGraph *mojom_files.MojomFileGraph
}

func (a StructVersions) Len() int           { return len(a) }
func (a StructVersions) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a StructVersions) Less(i, j int) bool { return a[i].Version < a[j].Version }

func (table *TypeTableTemplate) getTableForUDT(typeRef mojom_types.TypeReference) (elemTable string, elemType string, nullable bool) {
	nullable = typeRef.Nullable
	if typeRef.IsInterfaceRequest {
		elemTable = "NULL"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_HANDLE"
		return
	}
	if typeRef.TypeKey == nil {
		log.Fatalf("Unresolved type reference %s", typeRef.Identifier)
	}
	udt, _ := table.fileGraph.ResolvedTypes[*typeRef.TypeKey]
	switch udt.(type) {
	case *mojom_types.UserDefinedTypeStructType:
		structName := *udt.Interface().(mojom_types.MojomStruct).DeclData.FullIdentifier
		elemTable = "&" + mojomToCName(structName) + "__TypeDesc"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_STRUCT_PTR"
	case *mojom_types.UserDefinedTypeUnionType:
		unionName := *udt.Interface().(mojom_types.MojomUnion).DeclData.FullIdentifier
		elemTable = "&" + mojomToCName(unionName) + "__TypeDesc"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_UNION"
	case *mojom_types.UserDefinedTypeInterfaceType:
		elemTable = "NULL"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_INTERFACE"
	default:
		elemTable = "NULL"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_POD"
	}

	return
}

func (table *TypeTableTemplate) makeTableForType(prefix string, dataType mojom_types.Type) (elemTable string, elemType string, nullable bool) {
	switch dataType.(type) {
	case *mojom_types.TypeStringType:
		elemTable = "&g_mojom_string_type_description"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_ARRAY_PTR"
		nullable = dataType.Interface().(mojom_types.StringType).Nullable
	case *mojom_types.TypeArrayType:
		arrayTableName := fmt.Sprintf("%s_%d", prefix, table.counter)
		table.counter++
		typ := dataType.Interface().(mojom_types.ArrayType)
		elemTable = "&" + table.makeArrayPointerEntry(arrayTableName, typ)
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_ARRAY_PTR"
		nullable = typ.Nullable
	case *mojom_types.TypeMapType:
		mapTableName := fmt.Sprintf("%s_%d", prefix, table.counter)
		table.counter++
		typ := dataType.Interface().(mojom_types.MapType)
		elemTable = "&" + table.makeMapPointerTable(mapTableName, typ)
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_MAP_PTR"
		nullable = typ.Nullable
	case *mojom_types.TypeHandleType:
		typ := dataType.Interface().(mojom_types.HandleType)
		elemTable = "NULL"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_HANDLE"
		nullable = typ.Nullable
	case *mojom_types.TypeTypeReference:
		return table.getTableForUDT(dataType.Interface().(mojom_types.TypeReference))
	case *mojom_types.TypeSimpleType:
		elemTable = "NULL"
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_POD"
		nullable = false
	default:
		log.Fatal("uhoh, should not be here.")
	}

	return
}

// Returns the name of the array type table.
// Takes in |prefix|, the name this array table entry should be.
func (table *TypeTableTemplate) makeArrayPointerEntry(prefix string, f mojom_types.ArrayType) string {
	numElements := uint32(0)
	if f.FixedLength > 0 {
		numElements = uint32(f.FixedLength)
	}
	entry := ArrayPointerTableEntry{
		Name:        prefix + "__TypeDesc",
		NumElements: numElements,
		ElemNumBits: mojomTypeBitSize(f.ElementType, table.fileGraph),
		Nullable:    f.Nullable,
	}
	entry.ElemTable, entry.ElemType, entry.Nullable = table.makeTableForType(prefix, f.ElementType)

	table.Arrays = append(table.Arrays, entry)
	return entry.Name
}

func (table *TypeTableTemplate) makeMapPointerTable(prefix string, f mojom_types.MapType) string {
	structTable := StructPointerTable{
		Name: prefix + "__TypeDesc",
		Versions: []StructVersion{
			{
				Version:  0,
				NumBytes: 24, // A map has a struct header, and 2 pointers to arrays.
			},
		},
	}

	keyType := mojom_types.ArrayType{
		Nullable:    false,
		FixedLength: -1,
		ElementType: f.KeyType,
	}
	valueType := mojom_types.ArrayType{
		Nullable:    false,
		FixedLength: -1,
		ElementType: f.ValueType,
	}
	keyArray := table.makeArrayPointerEntry(prefix+"_Keys", keyType)
	valueArray := table.makeArrayPointerEntry(prefix+"_Values", valueType)

	// The key array has offset 0.
	// The value array has offset 8.
	structTable.Entries = append(structTable.Entries, StructPointerTableEntry{
		ElemTable:  "&" + keyArray,
		Offset:     0,
		MinVersion: 0,
		ElemType:   "MOJOM_TYPE_DESCRIPTOR_TYPE_ARRAY_PTR",
		Nullable:   false,
	})
	structTable.Entries = append(structTable.Entries, StructPointerTableEntry{
		ElemTable:  "&" + valueArray,
		Offset:     8,
		MinVersion: 0,
		ElemType:   "MOJOM_TYPE_DESCRIPTOR_TYPE_ARRAY_PTR",
		Nullable:   false,
	})
	table.Structs = append(table.Structs, structTable)
	return structTable.Name
}

// A union in a struct is inlined. It could possibly have a pointer type in
// there, so we consider unions to be pointers for the purposes of this method.
// TODO(vardhan): To optimize, check that, if union, there is a reference
// type inside the union before deeming the union a pointer type.
func (table *TypeTableTemplate) isPointerOrHandle(typ mojom_types.Type) bool {
	switch typ.(type) {
	case *mojom_types.TypeStringType:
		return true
	case *mojom_types.TypeArrayType:
		return true
	case *mojom_types.TypeMapType:
		return true
	case *mojom_types.TypeHandleType:
		return true
	case *mojom_types.TypeTypeReference:
		typRef := typ.Interface().(mojom_types.TypeReference)
		if udt, exists := table.fileGraph.ResolvedTypes[*typRef.TypeKey]; exists {
			switch udt.(type) {
			case *mojom_types.UserDefinedTypeStructType:
				return true
			case *mojom_types.UserDefinedTypeUnionType:
				return true
			case *mojom_types.UserDefinedTypeInterfaceType:
				return true
			}
		} else {
			log.Fatal("No such type reference.")
		}
	}
	return false
}

// Returns the StructPointerTableEntry pertaining to the given |fieldType|,
// but won't insert it into the `table`; it is the caller's responsibility to
// insert it. However, this operation is NOT totally immutable, since it may
// create type tables for sub types of |fieldType| (e.g. if fieldType is a map).
func (table *TypeTableTemplate) makeStructPointerTableEntry(prefix string, offset uint32, minVersion uint32, fieldType mojom_types.Type) StructPointerTableEntry {
	elemTableName := fmt.Sprintf("%s_%d", prefix, offset)
	elemTable, elemType, nullable := table.makeTableForType(elemTableName, fieldType)
	return StructPointerTableEntry{
		ElemTable:  elemTable,
		Offset:     offset,
		MinVersion: minVersion,
		ElemType:   elemType,
		Nullable:   nullable,
	}
}

// Given a MojomStruct, creates the templates required to make the type table
// for it, and inserts it into |table|.
func (table *TypeTableTemplate) insertStructPointerTable(s mojom_types.MojomStruct) {
	structTablePrefix := mojomToCName(*s.DeclData.FullIdentifier)
	structTable := StructPointerTable{
		Name: structTablePrefix + "__TypeDesc",
	}
	if s.VersionInfo != nil {
		for _, structVersion := range *s.VersionInfo {
			structTable.Versions = append(structTable.Versions, StructVersion{
				Version:  structVersion.VersionNumber,
				NumBytes: structVersion.NumBytes,
			})
		}
		// Sort by verion number in increasing order.
		sort.Sort(structTable.Versions)
	}

	for _, field := range s.Fields {
		if table.isPointerOrHandle(field.Type) {
			structTable.Entries = append(structTable.Entries, table.makeStructPointerTableEntry(
				structTablePrefix, uint32(field.Offset), field.MinVersion, field.Type))
		}
	}
	table.PublicStructNames = append(table.PublicStructNames, structTable.Name)
	table.Structs = append(table.Structs, structTable)
}

func (table *TypeTableTemplate) makeUnionPointerTableEntry(prefix string, tag uint32, fieldType mojom_types.Type) UnionPointerTableEntry {
	elemTableName := fmt.Sprintf("%s_%d", prefix, tag)
	elemTable, elemType, nullable := table.makeTableForType(elemTableName, fieldType)
	if elemType == "MOJOM_TYPE_DESCRIPTOR_TYPE_UNION" {
		elemType = "MOJOM_TYPE_DESCRIPTOR_TYPE_UNION_PTR"
	}
	return UnionPointerTableEntry{
		ElemTable: elemTable,
		Tag:       tag,
		Nullable:  nullable,
		ElemType:  elemType,
	}
}

// Given a MojomUnion, creates the templates required to make the type table
// for it, and inserts it into |table|.
func (table *TypeTableTemplate) insertUnionPointerTable(u mojom_types.MojomUnion) {
	unionTablePrefix := mojomToCName(*u.DeclData.FullIdentifier)
	unionTable := UnionPointerTable{
		Name:      unionTablePrefix + "__TypeDesc",
		NumFields: uint32(len(u.Fields)),
	}
	for _, field := range u.Fields {
		if table.isPointerOrHandle(field.Type) {
			unionTable.Entries = append(unionTable.Entries, table.makeUnionPointerTableEntry(unionTablePrefix, uint32(field.Tag), field.Type))
		}
	}
	table.PublicUnionNames = append(table.PublicUnionNames, unionTable.Name)
	table.Unions = append(table.Unions, unionTable)
}

func NewTypeTableTemplate(fileGraph *mojom_files.MojomFileGraph, file *mojom_files.MojomFile) TypeTableTemplate {
	table := TypeTableTemplate{
		fileGraph: fileGraph,
	}
	if file.DeclaredMojomObjects.Structs != nil {
		for _, mojomStructKey := range *(file.DeclaredMojomObjects.Structs) {
			mojomStruct := fileGraph.ResolvedTypes[mojomStructKey].Interface().(mojom_types.MojomStruct)
			table.insertStructPointerTable(mojomStruct)
		}
	}
	if file.DeclaredMojomObjects.Unions != nil {
		for _, mojomUnionKey := range *(file.DeclaredMojomObjects.Unions) {
			mojomUnion := fileGraph.ResolvedTypes[mojomUnionKey].Interface().(mojom_types.MojomUnion)
			table.insertUnionPointerTable(mojomUnion)
		}
	}
	if file.DeclaredMojomObjects.Interfaces != nil {
		for _, mojomInterfaceKey := range *(file.DeclaredMojomObjects.Interfaces) {
			mojomIface := fileGraph.ResolvedTypes[mojomInterfaceKey].Interface().(mojom_types.MojomInterface)
			for _, mojomMethod := range mojomIface.Methods {
				params := mojomMethod.Parameters
				fullId := requestMethodToCName(&mojomIface, &params)
				params.DeclData.FullIdentifier = &fullId
				table.insertStructPointerTable(params)
				if mojomMethod.ResponseParams != nil {
					params := *mojomMethod.ResponseParams
					fullId := responseMethodToCName(&mojomIface, &params)
					params.DeclData.FullIdentifier = &fullId
					table.insertStructPointerTable(params)
				}
			}
		}
	}
	return table
}
