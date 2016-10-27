// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cgen

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

// TODO(vardhan): Make this file unittestable? This involves making it not crash
// on failure (so that we can test failure).
// Translates: path/to/file.mojom -> PATH_TO_FILE_MOJOM_C_H_
func toHeaderGuard(path string) string {
	return strings.Replace(strings.Replace(strings.Replace(strings.ToUpper(path), string(os.PathSeparator), "_", -1), "-", "_", -1), ".", "_", -1) + "_C_H_"
}

var reservedCKeywords map[string]bool = map[string]bool{
	"_Alignas":       true,
	"_Alignof":       true,
	"_Atomic":        true,
	"_Bool":          true,
	"_Complex":       true,
	"_Generic":       true,
	"_Imaginary":     true,
	"_Noreturn":      true,
	"_Static_assert": true,
	"_Thread_local":  true,
	"alignas":        true,
	"alignof":        true,
	"auto":           true,
	"bool":           true,
	"break":          true,
	"case":           true,
	"char":           true,
	"complex":        true,
	"const":          true,
	"continue":       true,
	"default":        true,
	"do":             true,
	"double":         true,
	"else":           true,
	"enum":           true,
	"extern":         true,
	"float":          true,
	"for":            true,
	"goto":           true,
	"if":             true,
	"imaginary":      true,
	"inline":         true,
	"int":            true,
	"long":           true,
	"noreturn":       true,
	"register":       true,
	"restrict":       true,
	"return":         true,
	"short":          true,
	"signed":         true,
	"sizeof":         true,
	"static":         true,
	"static_assert":  true,
	"struct":         true,
	"switch":         true,
	"typedef":        true,
	"union":          true,
	"unsigned":       true,
	"void":           true,
	"volatile":       true,
	"while":          true,
}

func isReservedKeyword(keyword string) bool {
	_, found := reservedCKeywords[keyword]
	return found
}

// Translates: import "file.mojom" -> #include 'rel/path/file.mojom-c.h'
func mojomToCFilePath(srcRootPath string, mojomImport string) string {
	rel_import, err := filepath.Rel(srcRootPath, mojomImport)
	if err != nil {
		log.Fatalf("Cannot determine relative path for '%s'", mojomImport)
	}
	return rel_import + "-c.h"
}

// Given an interface + method name, return a name-mangled name in C.
// TODO(vardhan): You don't need to pass an entire MojomInterface/MojomStruct
// here.
func requestMethodToCName(mojomIface *mojom_types.MojomInterface, mojomStruct *mojom_types.MojomStruct) string {
	prefix := mojomToCName(*mojomIface.DeclData.FullIdentifier)
	return fmt.Sprintf("%s_%s", prefix, strings.Replace(*mojomStruct.DeclData.ShortName, "-request", "_Request", 1))
}

func responseMethodToCName(mojomIface *mojom_types.MojomInterface, mojomStruct *mojom_types.MojomStruct) string {
	prefix := mojomToCName(*mojomIface.DeclData.FullIdentifier)
	return fmt.Sprintf("%s_%s", prefix, strings.Replace(*mojomStruct.DeclData.ShortName, "-response", "_Response", 1))
}

// Given a name (described in mojom IDL), return a C-managled name.
func mojomToCName(name string) string {
	return strings.Replace(name, ".", "_", -1)
}

func mojomToCBuiltinValue(val mojom_types.BuiltinConstantValue) string {
	switch val {
	case mojom_types.BuiltinConstantValue_DoubleNegativeInfinity:
		return "-INFINITY"
	case mojom_types.BuiltinConstantValue_FloatNegativeInfinity:
		return "-INFINITY"
	case mojom_types.BuiltinConstantValue_DoubleInfinity:
		return "INFINITY"
	case mojom_types.BuiltinConstantValue_FloatInfinity:
		return "INFINITY"
	case mojom_types.BuiltinConstantValue_DoubleNan:
		return "NAN"
	case mojom_types.BuiltinConstantValue_FloatNan:
		return "NAN"
	}
	log.Fatal("Unknown builtin constant", val)
	return ""
}

// A mojom type name-mangled to C type.
func mojomToCType(t mojom_types.Type, fileGraph *mojom_files.MojomFileGraph) string {
	switch t.(type) {
	case *mojom_types.TypeSimpleType:
		return simpleTypeToCType(t.Interface().(mojom_types.SimpleType))
	case *mojom_types.TypeArrayType:
		return "union MojomArrayHeaderPtr"
	case *mojom_types.TypeMapType:
		return "union MojomMapHeaderPtr"
	case *mojom_types.TypeStringType:
		return "union MojomStringHeaderPtr"
	case *mojom_types.TypeHandleType:
		return "MojoHandle"
	case *mojom_types.TypeTypeReference:
		typ_ref := t.Interface().(mojom_types.TypeReference)
		if typ_ref.IsInterfaceRequest {
			return "MojoHandle"
		}
		resolved_type := fileGraph.ResolvedTypes[*typ_ref.TypeKey]
		return userDefinedTypeToCType(&resolved_type)
	default:
		log.Fatal("Should not be here! ", t.Interface())
	}

	return ""
}

func mojomToCLiteral(value mojom_types.LiteralValue) string {
	val := value.Interface()
	switch value.(type) {
	case *mojom_types.LiteralValueBoolValue:
		return strconv.FormatBool(val.(bool))
	case *mojom_types.LiteralValueDoubleValue:
		return strconv.FormatFloat(val.(float64), 'e', -1, 64)
	case *mojom_types.LiteralValueFloatValue:
		return strconv.FormatFloat(val.(float64), 'e', -1, 32)
	case *mojom_types.LiteralValueInt8Value:
		return strconv.FormatInt(int64(val.(int8)), 10)
	case *mojom_types.LiteralValueInt16Value:
		return strconv.FormatInt(int64(val.(int16)), 10)
	case *mojom_types.LiteralValueInt32Value:
		return strconv.FormatInt(int64(val.(int32)), 10) + "l"
	case *mojom_types.LiteralValueInt64Value:
		return strconv.FormatInt(int64(val.(int64)), 10) + "ll"
	case *mojom_types.LiteralValueUint8Value:
		return strconv.FormatUint(uint64(val.(uint8)), 10)
	case *mojom_types.LiteralValueUint16Value:
		return strconv.FormatUint(uint64(val.(uint16)), 10)
	case *mojom_types.LiteralValueUint32Value:
		return strconv.FormatUint(uint64(val.(uint32)), 10) + "ul"
	case *mojom_types.LiteralValueUint64Value:
		return strconv.FormatUint(uint64(val.(uint64)), 10) + "ull"
	case *mojom_types.LiteralValueStringValue:
		// TODO(vardhan): Do we have to do any string escaping here?
		return "\"" + val.(string) + "\""
	default:
		log.Fatal("Should not reach here. Unknown literal type:", value)
	}

	return ""
}

func mojomUnionToCType(u *mojom_types.MojomUnion) string {
	return fmt.Sprintf("union %sPtr", mojomToCName(*u.DeclData.FullIdentifier))
}

func userDefinedTypeToCType(t *mojom_types.UserDefinedType) string {
	switch (*t).(type) {
	case *mojom_types.UserDefinedTypeStructType:
		full_name := mojomToCName(*(*t).Interface().(mojom_types.MojomStruct).DeclData.FullIdentifier)
		return "union " + full_name + "Ptr"
	case *mojom_types.UserDefinedTypeEnumType:
		full_name := mojomToCName(*(*t).Interface().(mojom_types.MojomEnum).DeclData.FullIdentifier)
		return full_name
	case *mojom_types.UserDefinedTypeUnionType:
		full_name := mojomToCName(*(*t).Interface().(mojom_types.MojomUnion).DeclData.FullIdentifier)
		// Caller beware: this should instead be "union %sPtr" if this type is to
		// be used inside a union instead (since union inside a union is a
		// reference, not inlined).
		return "struct " + full_name
	case *mojom_types.UserDefinedTypeInterfaceType:
		return "struct MojomInterfaceData"
	}
	log.Fatal("Unknown UserDefinedType", t)
	return ""
}

func simpleTypeToCType(st mojom_types.SimpleType) string {
	switch st {
	case mojom_types.SimpleType_Int8:
		return "int8_t"
	case mojom_types.SimpleType_Int16:
		return "int16_t"
	case mojom_types.SimpleType_Int32:
		return "int32_t"
	case mojom_types.SimpleType_Int64:
		return "int64_t"
	case mojom_types.SimpleType_Uint8:
		return "uint8_t"
	case mojom_types.SimpleType_Uint16:
		return "uint16_t"
	case mojom_types.SimpleType_Uint32:
		return "uint32_t"
	case mojom_types.SimpleType_Uint64:
		return "uint64_t"
	case mojom_types.SimpleType_Float:
		return "float"
	case mojom_types.SimpleType_Double:
		return "double"
	case mojom_types.SimpleType_Bool:
		return "bool"
	default:
		log.Fatal("Unknown Simple type:", st)
	}
	return ""
}

func mojomTypeIsBool(typ mojom_types.Type) bool {
	switch typ.(type) {
	case *mojom_types.TypeSimpleType:
		if typ.Interface().(mojom_types.SimpleType) == mojom_types.SimpleType_Bool {
			return true
		}
	}
	return false
}

func mojomTypeIsUnion(typ mojom_types.Type, fileGraph *mojom_files.MojomFileGraph) bool {
	switch typ.(type) {
	case *mojom_types.TypeTypeReference:
		type_key := *typ.Interface().(mojom_types.TypeReference).TypeKey
		switch fileGraph.ResolvedTypes[type_key].(type) {
		case *mojom_types.UserDefinedTypeUnionType:
			return true
		}
	}
	return false
}

// Returns the inlined (in a struct or array) size of a type in bytes.
func mojomTypeByteSize(typ mojom_types.Type, fileGraph *mojom_files.MojomFileGraph) uint32 {
	switch typ.(type) {
	case *mojom_types.TypeSimpleType:
		switch typ.Interface().(mojom_types.SimpleType) {
		case mojom_types.SimpleType_Bool:
			return 1
		case mojom_types.SimpleType_Double:
			return 8
		case mojom_types.SimpleType_Float:
			return 4
		case mojom_types.SimpleType_Int8:
			return 1
		case mojom_types.SimpleType_Int16:
			return 2
		case mojom_types.SimpleType_Int32:
			return 4
		case mojom_types.SimpleType_Int64:
			return 8
		case mojom_types.SimpleType_Uint8:
			return 1
		case mojom_types.SimpleType_Uint16:
			return 2
		case mojom_types.SimpleType_Uint32:
			return 4
		case mojom_types.SimpleType_Uint64:
			return 8
		default:
			log.Fatal("Invalid simple type:", typ.Interface().(mojom_types.SimpleType))
		}
	case *mojom_types.TypeStringType:
		return 8
	case *mojom_types.TypeArrayType:
		return 8
	case *mojom_types.TypeMapType:
		return 8
	case *mojom_types.TypeHandleType:
		return 4
	case *mojom_types.TypeTypeReference:
		return referenceTypeSize(typ.Interface().(mojom_types.TypeReference), fileGraph)
	default:
		log.Fatal("Should not be here. Unknown mojom type size for:", typ)
	}
	return 0
}

// Returns the inlined (in a struct or array) size of a reference type.
func referenceTypeSize(typ mojom_types.TypeReference, fileGraph *mojom_files.MojomFileGraph) uint32 {
	if typ.IsInterfaceRequest {
		return 4
	}

	udt := fileGraph.ResolvedTypes[*typ.TypeKey]
	switch udt.(type) {
	case *mojom_types.UserDefinedTypeEnumType:
		return 4
	case *mojom_types.UserDefinedTypeStructType:
		return 8
	case *mojom_types.UserDefinedTypeUnionType:
		return 16
	case *mojom_types.UserDefinedTypeInterfaceType:
		return 8
	}
	log.Fatal("Uhoh, should not be here.", udt.Interface())
	return 0
}

// Same as mojomTypeByteSize, but in bits.
func mojomTypeBitSize(typ mojom_types.Type, fileGraph *mojom_files.MojomFileGraph) uint32 {
	switch typ.(type) {
	case *mojom_types.TypeSimpleType:
		switch typ.Interface().(mojom_types.SimpleType) {
		case mojom_types.SimpleType_Bool:
			return 1
		}
	}

	return 8 * mojomTypeByteSize(typ, fileGraph)
}
