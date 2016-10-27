// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rustgen

import (
	"log"
	"strconv"
	"strings"

	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
)

// TODO(vardhan): Make this file unittestable? This involves making it not crash
// on failure (so that we can test failure).
var reservedRustKeywords map[string]bool = map[string]bool{
	"as":           true,
	"box":          true,
	"break":        true,
	"const":        true,
	"continue":     true,
	"crate":	true,
	"else":		true,
	"enum":		true,
	"extern":       true,
	"false":	true,
	"fn":		true,
	"for":		true,
	"if":		true,
	"impl":		true,
	"in":		true,
	"let":		true,
	"loop":		true,
	"match":	true,
	"mod":		true,
	"move":		true,
	"mut":		true,
	"pub":		true,
	"ref":		true,
	"return":	true,
	"self":		true,
	"Self":		true,
	"static":       true,
	"struct":       true,
	"super":	true,
	"trait":	true,
	"true":		true,
	"type":		true,
	"unsafe":       true,
	"use":		true,
	"where":	true,
	"while":        true,
	// Keywords reserved for future use (future-proofing...)
	"abstract":	true,
	"alignof":	true,
	"become":	true,
	"do":		true,
	"final":	true,
	"macro":	true,
	"offsetof":	true,
	"override":	true,
	"priv":		true,
	"proc":		true,
	"pure":		true,
	"sizeof":	true,
	"typeof":	true,
	"unsized":	true,
	"virtual":	true,
	"yield":	true,
	// Weak keywords (special meaning in specific contexts)
	"default":	true,
	"'static":	true,
	"union":	true,
}

func isReservedKeyword(keyword string) bool {
	_, found := reservedRustKeywords[keyword]
	return found
}

func assertNotReservedKeyword(keyword string) string {
	if isReservedKeyword(keyword) {
		log.Fatal("Generated name `%s` is a reserved Rust keyword.")
	}
	return keyword
}

// Mangles a name.
func mangleName(name string) string {
	return name + "_"
}

// A map where keys are Mojom names and values are Rust-mangled names.
type Names map[string]string

// Runs through the file graph and collects all names putting them into a Names map which
// is a map from the original name to the mangled name
func CollectAllNames(fileGraph *mojom_files.MojomFileGraph, file *mojom_files.MojomFile) *Names {
	names := make(Names)
	// Collect all top-level constant names
	if file.DeclaredMojomObjects.TopLevelConstants != nil {
		for _, mojomConstKey := range *(file.DeclaredMojomObjects.TopLevelConstants) {
			mojomConst := fileGraph.ResolvedConstants[mojomConstKey]
			names[*mojomConst.DeclData.ShortName] = *mojomConst.DeclData.ShortName
		}
	}

	// Collect all top-level enum names
	if file.DeclaredMojomObjects.TopLevelEnums != nil {
		for _, mojomEnumKey := range *(file.DeclaredMojomObjects.TopLevelEnums) {
			mojomEnum := fileGraph.ResolvedTypes[mojomEnumKey].Interface().(mojom_types.MojomEnum)
			names[*mojomEnum.DeclData.ShortName] = *mojomEnum.DeclData.ShortName
		}
	}

	// Collect all union names
	if file.DeclaredMojomObjects.Unions != nil {
		for _, mojomUnionKey := range *(file.DeclaredMojomObjects.Unions) {
			mojomUnion := fileGraph.ResolvedTypes[mojomUnionKey].Interface().(mojom_types.MojomUnion)
			names[*mojomUnion.DeclData.ShortName] = *mojomUnion.DeclData.ShortName
		}
	}

	// Collect all struct names
	if file.DeclaredMojomObjects.Structs != nil {
		for _, mojomStructKey := range *(file.DeclaredMojomObjects.Structs) {
			mojomStruct := fileGraph.ResolvedTypes[mojomStructKey].Interface().(mojom_types.MojomStruct)
			names[*mojomStruct.DeclData.ShortName] = *mojomStruct.DeclData.ShortName
		}
	}

	// Collect all interface names
	if file.DeclaredMojomObjects.Interfaces != nil {
		for _, mojomIfaceKey := range *(file.DeclaredMojomObjects.Interfaces) {
			mojomIface := fileGraph.ResolvedTypes[mojomIfaceKey].Interface().(mojom_types.MojomInterface)
			names[*mojomIface.DeclData.ShortName] = *mojomIface.DeclData.ShortName
			// Pre-mangle all the method names since we know exactly how they look
			for _, mojomMethod := range mojomIface.Methods {
				method_name := *mojomMethod.DeclData.ShortName
				names[method_name + "-request"] = method_name + "Request"
				names[method_name + "-response"] = method_name + "Response"
			}
		}
	}
	// Contained declarations do not need to be handled because they are prepended with
	// their mangled parent's names.
	return &names
}

// Mangles keywords found in the map until there are no more conflicts.
func (n *Names) MangleKeywords() {
	for name := range *n {
		if isReservedKeyword(name) {
			new_name := name
			for (*n)[new_name] != "" {
				new_name = mangleName(new_name)
			}
			(*n)[name] = new_name
		}
	}
}

// Removes the namespace from a full identifier are returns the mangled name
// for the identifier or type.
func removeNamespaceFromIdentifier(context *Context, file *mojom_files.MojomFile, ident string) string {
	var namespace string
	if file.ModuleNamespace != nil {
		namespace = *file.ModuleNamespace + "."
	} else {
		namespace = ""
	}
	name := strings.Replace(ident, namespace, "", 1)
	portions := strings.Split(name, ".")
	portions[0] = context.GetNameFromFile(portions[0], file)
	return strings.Join(portions, "")
}

// Given a name (described in mojom IDL), return a Rust-managled name.
func mojomToRustName(decl *mojom_types.DeclarationData, context *Context) string {
	// Might not have a full identifier; use a ShortName instead
	if decl.FullIdentifier == nil {
		return context.GetName(*decl.ShortName)
	}
	// No need to specify path if the name is defined in the current file
	src_file_name := decl.SourceFileInfo.FileName
	if src_file_name == context.File.FileName {
		return removeNamespaceFromIdentifier(context, context.File, *decl.FullIdentifier)
	}
	src_file := context.FileGraph.Files[src_file_name]
	// Figure out the path to the imported item
	path := rustImportPath(context.File, &src_file)
	name := removeNamespaceFromIdentifier(context, &src_file, *decl.FullIdentifier)
	return path + "::" + name
}

func mojomToRustBuiltinValue(val mojom_types.BuiltinConstantValue) string {
	switch val {
	case mojom_types.BuiltinConstantValue_DoubleNegativeInfinity:
		return "std::f64::NEG_INFINITY"
	case mojom_types.BuiltinConstantValue_FloatNegativeInfinity:
		return "std::f32::NEG_INFINITY"
	case mojom_types.BuiltinConstantValue_DoubleInfinity:
		return "std::f64::INFINITY"
	case mojom_types.BuiltinConstantValue_FloatInfinity:
		return "std::f32::INFINITY"
	case mojom_types.BuiltinConstantValue_DoubleNan:
		return "std::f64::NAN"
	case mojom_types.BuiltinConstantValue_FloatNan:
		return "std::f32::NAN"
	}
	log.Fatal("Unknown builtin constant", val)
	return ""
}

// A mojom type name-mangled to Rust type.
func mojomToRustType(t mojom_types.Type, context *Context) string {
	switch t.(type) {
	case *mojom_types.TypeSimpleType:
		return simpleTypeToRustType(t.Interface().(mojom_types.SimpleType))
	case *mojom_types.TypeArrayType:
		array_type := t.Interface().(mojom_types.ArrayType)
		elem_type_str := mojomToRustType(array_type.ElementType, context)
		var type_str string
		if array_type.FixedLength < 0 {
			type_str = "Vec<" + elem_type_str + ">"
		} else if array_type.FixedLength <= 32 {
			// Regrettably, because Rust doesn't allow you to be generic over
			// fixed array sizes, we only support checking lengths of up to
			// and including 32.
			len_str := strconv.Itoa(int(array_type.FixedLength))
			type_str = "[" + elem_type_str + "; " + len_str + "]"
		} else {
			log.Fatal("Rust doesn't currently support arrays of " +
				  "fixed size greater than 32. The reason for this is " +
				  "the lack of generics over array length in the type " +
				  "system. Since we cannot encode the length in the type " +
				  "there is no way for nested decode routines to know " +
				  "what length it should be. Sorry. :(")
			// Fixed length array, but user has to validate the length themselves.
			//type_str = "Box<[" + elem_type_str + "]>"
		}
		if array_type.Nullable {
			return "Option<" + type_str + ">"
		}
		return type_str
	case *mojom_types.TypeMapType:
		map_type := t.Interface().(mojom_types.MapType)
		key_type_str := mojomToRustType(map_type.KeyType, context)
		value_type_str := mojomToRustType(map_type.ValueType, context)
		type_str := "HashMap<" + key_type_str + "," + value_type_str + ">"
		if map_type.Nullable {
			return "Option<" + type_str + ">"
		}
		return type_str
	case *mojom_types.TypeStringType:
		string_type := t.Interface().(mojom_types.StringType)
		if string_type.Nullable {
			return "Option<String>"
		}
		return "String"
	case *mojom_types.TypeHandleType:
		handle_type := t.Interface().(mojom_types.HandleType)
		var type_str string
		switch handle_type.Kind {
		case mojom_types.HandleType_Kind_MessagePipe:
			type_str = "message_pipe::MessageEndpoint"
		case mojom_types.HandleType_Kind_DataPipeConsumer:
			type_str = "system::data_pipe::Consumer<u8>"
		case mojom_types.HandleType_Kind_DataPipeProducer:
			type_str = "system::data_pipe::Producer<u8>"
		case mojom_types.HandleType_Kind_SharedBuffer:
			type_str = "system::shared_buffer::SharedBuffer"
		case mojom_types.HandleType_Kind_Unspecified:
			type_str = "system::UntypedHandle"
		default:
			log.Fatal("Unknown handle type kind! ", handle_type.Kind)
		}
		if handle_type.Nullable {
			return "Option<" + type_str + ">"
		}
		return type_str
	case *mojom_types.TypeTypeReference:
		typ_ref := t.Interface().(mojom_types.TypeReference)
		resolved_type := context.FileGraph.ResolvedTypes[*typ_ref.TypeKey]
		type_str := userDefinedTypeToRustType(&resolved_type, context, typ_ref.IsInterfaceRequest)
		if typ_ref.Nullable {
			return "Option<" + type_str + ">"
		}
		return type_str
	default:
		log.Fatal("Should not be here! ", t.Interface())
	}

	return ""
}

func mojomToRustLiteral(value mojom_types.LiteralValue) string {
	val := value.Interface()
	switch value.(type) {
	case *mojom_types.LiteralValueBoolValue:
		return strconv.FormatBool(val.(bool))
	case *mojom_types.LiteralValueDoubleValue:
		return strconv.FormatFloat(val.(float64), 'e', -1, 64) + "f64"
	case *mojom_types.LiteralValueFloatValue:
		return strconv.FormatFloat(val.(float64), 'e', -1, 32) + "f32"
	case *mojom_types.LiteralValueInt8Value:
		return strconv.FormatInt(int64(val.(int8)), 10) + "i8"
	case *mojom_types.LiteralValueInt16Value:
		return strconv.FormatInt(int64(val.(int16)), 10) + "i16"
	case *mojom_types.LiteralValueInt32Value:
		return strconv.FormatInt(int64(val.(int32)), 10) + "i32"
	case *mojom_types.LiteralValueInt64Value:
		return strconv.FormatInt(int64(val.(int64)), 10) + "i64"
	case *mojom_types.LiteralValueUint8Value:
		return strconv.FormatUint(uint64(val.(uint8)), 10) + "u8"
	case *mojom_types.LiteralValueUint16Value:
		return strconv.FormatUint(uint64(val.(uint16)), 10) + "u16"
	case *mojom_types.LiteralValueUint32Value:
		return strconv.FormatUint(uint64(val.(uint32)), 10) + "u32"
	case *mojom_types.LiteralValueUint64Value:
		return strconv.FormatUint(uint64(val.(uint64)), 10) + "u64"
	case *mojom_types.LiteralValueStringValue:
		// TODO(mknyszek): Do we have to do any string escaping here?
		return "\"" + val.(string) + "\"" + ".to_string()"
	default:
		log.Fatal("Should not reach here. Unknown literal type:", value)
	}

	return ""
}

func mojomUnionToRustType(u *mojom_types.MojomUnion, context *Context) string {
	return mojomToRustName(u.DeclData, context)
}

func userDefinedTypeToRustType(t *mojom_types.UserDefinedType, context *Context, interfaceRequest bool) string {
	switch (*t).(type) {
	case *mojom_types.UserDefinedTypeStructType:
		full_name := mojomToRustName((*t).Interface().(mojom_types.MojomStruct).DeclData, context)
		return full_name
	case *mojom_types.UserDefinedTypeEnumType:
		full_name := mojomToRustName((*t).Interface().(mojom_types.MojomEnum).DeclData, context)
		return full_name
	case *mojom_types.UserDefinedTypeUnionType:
		full_name := mojomToRustName((*t).Interface().(mojom_types.MojomUnion).DeclData, context)
		return full_name
	case *mojom_types.UserDefinedTypeInterfaceType:
		full_name := mojomToRustName((*t).Interface().(mojom_types.MojomInterface).DeclData, context)
		if interfaceRequest {
			return full_name + "Server"
		}
		return full_name + "Client"
	}
	log.Fatal("Unknown UserDefinedType", t)
	return ""
}

func simpleTypeToRustType(st mojom_types.SimpleType) string {
	switch st {
	case mojom_types.SimpleType_Int8:
		return "i8"
	case mojom_types.SimpleType_Int16:
		return "i16"
	case mojom_types.SimpleType_Int32:
		return "i32"
	case mojom_types.SimpleType_Int64:
		return "i64"
	case mojom_types.SimpleType_Uint8:
		return "u8"
	case mojom_types.SimpleType_Uint16:
		return "u16"
	case mojom_types.SimpleType_Uint32:
		return "u32"
	case mojom_types.SimpleType_Uint64:
		return "u64"
	case mojom_types.SimpleType_Float:
		return "f32"
	case mojom_types.SimpleType_Double:
		return "f64"
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

func mojomTypeIsUnion(typ mojom_types.Type, context *Context) bool {
	switch typ.(type) {
	case *mojom_types.TypeTypeReference:
		type_key := *typ.Interface().(mojom_types.TypeReference).TypeKey
		switch context.FileGraph.ResolvedTypes[type_key].(type) {
		case *mojom_types.UserDefinedTypeUnionType:
			return true
		}
	}
	return false
}

func mojomAlignToBytes(size uint32, alignment uint32) uint32 {
	diff := size % alignment
	if alignment == 0 || diff == 0 {
		return size
	}
	return size + alignment - diff
}

func mojomTypeSize(typ mojom_types.Type, context *Context) uint32 {
	switch typ.(type) {
	case *mojom_types.TypeSimpleType:
		switch typ.Interface().(mojom_types.SimpleType) {
		case mojom_types.SimpleType_Bool:
			log.Fatal("Simple type bool does not have a size in bytes!")
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
		return referenceTypeSize(typ.Interface().(mojom_types.TypeReference), context)
	default:
		log.Fatal("Should not be here. Unknown mojom type size for:", typ)
	}
	return 0
}

func referenceTypeSize(typ mojom_types.TypeReference, context *Context) uint32 {
	if typ.IsInterfaceRequest {
		return 4
	}

	udt := context.FileGraph.ResolvedTypes[*typ.TypeKey]
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
