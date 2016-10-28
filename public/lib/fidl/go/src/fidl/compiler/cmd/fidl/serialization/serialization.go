// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serialization

import (
	"bytes"
	"compress/gzip"
	"encoding/base64"
	"fidl/bindings"
	myfmt "fidl/compiler/cmd/fidl/third_party/golang/src/fmt"
	"fidl/compiler/core"
	"fidl/compiler/generated/fidl_files"
	"fidl/compiler/generated/fidl_types"
	"fmt"
)

//////////////////////////////////////////////////
/// Fidl Descriptor Serialization
//////////////////////////////////////////////////

// This variable may be set to false in order to omit emitting line and
// column numbers. This is useful in tests.
var emitLineAndColumnNumbers bool = true

// This variable may be set to false in order to omit emitting computed
// packing. This is useful in tests.
var emitComputedPackingData bool = true

// This variable may be set to false in order to omit emitting serialized
// runtime type info. This is useful in tests.
var emitSerializedRuntimeTypeInfo bool = true

// Serialize serializes the FidlDescriptor into a binary form that is passed to the
// backend of the compiler in order to invoke the code generators.
// To do this we use Mojo serialization.
// If |debug| is true we also return a human-readable representation
// of the serialized fidl_types.FileGraph.
// This function is not thread safe.
func Serialize(d *core.MojomDescriptor, debug bool) (bytes []byte, debugString string, err error) {
	return serialize(d, debug, true, true, true)
}

// serialize() is a package-private version of the public method Serialize().
// It is intended for use in tests because it allows setting of the variables
// emitLineAndColumnNumbers, emitComputedPackingData and emitSerializedRuntimeTypeInfo.
// This function is not thread safe because it sets and accesses these global
// variables.
func serialize(d *core.MojomDescriptor, debug,
	emitLineAndColumnNumbersParam, emitComputedPackingDataParam,
	emitSerializedRuntimeTypeInfoParam bool) (bytes []byte, debugString string, err error) {

	// Save the global state and then set it based on the parameters.
	saveEmitLineAndColumnNumbers := emitLineAndColumnNumbers
	emitLineAndColumnNumbers = emitLineAndColumnNumbersParam
	saveEmitComputedPackingData := emitComputedPackingData
	emitComputedPackingData = emitComputedPackingDataParam
	saveEmitSerializedRuntimeTypeInfo := emitSerializedRuntimeTypeInfo
	emitSerializedRuntimeTypeInfo = emitSerializedRuntimeTypeInfoParam

	fileGraph := translateDescriptor(d)
	if debug {
		debugString = myfmt.Sprintf("%#v", fileGraph)
	}
	encoder := bindings.NewEncoder()
	encoder.SetDeterministic(true)
	fileGraph.Encode(encoder)
	bytes, _, err = encoder.Data()

	// Restore the original values of the global state.
	emitLineAndColumnNumbers = saveEmitLineAndColumnNumbers
	emitComputedPackingData = saveEmitComputedPackingData
	emitSerializedRuntimeTypeInfo = saveEmitSerializedRuntimeTypeInfo
	return
}

// translateDescriptor translates from a core.MojomDescriptor (the pure Go
// representation used by the parser) to a fidl_files.FidlFileGraph (the
// Mojo Go representation used for serialization.)
func translateDescriptor(d *core.MojomDescriptor) *fidl_files.FidlFileGraph {
	fileGraph := fidl_files.FidlFileGraph{}

	// Add |resolved_types| field.
	fileGraph.ResolvedTypes = make(map[string]fidl_types.UserDefinedType)
	for key, userDefinedType := range d.TypesByKey {
		fileGraph.ResolvedTypes[key] = translateUserDefinedType(userDefinedType)
	}

	// Add |resolved_constants| field.
	fileGraph.ResolvedConstants = make(map[string]fidl_types.DeclaredConstant)
	for key, userDefinedValue := range d.ValuesByKey {
		switch c := userDefinedValue.(type) {
		// Note that our representation of values in fidl_types.fidl is a little different than our
		// pure Go representation. In the latter we use value keys to refer to both constants and
		// enum values but in the former we only use value keys to refer to constants. Enum values
		// are stored as part of their enum types and they are are referred to  not directly using
		// value keyes but rather via the type key of their enum and an index into the |values| array
		// of that enum. For this reason we are only looking for the constants here and ignoring the
		// enum values. Thos will get translated when the
		case *core.UserDefinedConstant:
			fileGraph.ResolvedConstants[key] = translateUserDefinedConstant(c)
		}
	}

	// Add |files| field.
	fileGraph.Files = make(map[string]fidl_files.FidlFile)
	for name, file := range d.MojomFilesByName {
		fileGraph.Files[name] = translateFidlFile(file, &fileGraph)
	}

	return &fileGraph
}

// translateFidlFile translates from a core.MojomFile (the pure Go
// representation used by the parser) to a fidl_files.FidlFile (the
// Mojo Go representation used for serialization.)
func translateFidlFile(f *core.MojomFile, fileGraph *fidl_files.FidlFileGraph) (file fidl_files.FidlFile) {
	// file_name field
	file.FileName = f.CanonicalFileName

	// specified_file_name_field
	file.SpecifiedFileName = &f.SpecifiedFileName

	// module_namespace field
	file.ModuleNamespace = &f.ModuleNamespace.Identifier

	// attributes field
	if f.Attributes != nil {
		file.Attributes = new([]fidl_types.Attribute)
		for _, attr := range f.Attributes.List {
			*(file.Attributes) = append(*(file.Attributes), translateFidlAttribute(&attr))
		}
	}

	// imports field
	if len(f.Imports) > 0 {
		file.Imports = new([]string)
		for _, importName := range f.Imports {
			// We support serializing a FidlFileGraph that has not had import names
			// resolved to canonical file names. (For example this is the case in
			// meta-data-only mode.)
			if importName.CanonicalFileName != "" {
				*(file.Imports) = append(*(file.Imports), importName.CanonicalFileName)
			}
		}
		if len(*file.Imports) == 0 {
			// If we are in a mode where canonical file names are not being resolved
			// then emit a null value for |imports| rather than an empty array.
			file.Imports = nil
		}
	}

	// We will populate a RuntimeTypeInfo structure and then serialize it and
	// the serialized bytes will form the |serialized_runtime_type_info| field
	// of the FidlFile.
	typeInfo := fidl_types.RuntimeTypeInfo{}
	typeInfo.Services = make(map[string]string)
	typeInfo.TypeMap = make(map[string]fidl_types.UserDefinedType)

	// We populate the declared_fidl_objects field
	// and simultaneously we populate typeInfo.TypeMap.

	// Interfaces
	if f.Interfaces != nil && len(f.Interfaces) > 0 {
		file.DeclaredFidlObjects.Interfaces = new([]string)
		for _, intrfc := range f.Interfaces {
			typeKey := intrfc.TypeKey()
			*(file.DeclaredFidlObjects.Interfaces) = append(*(file.DeclaredFidlObjects.Interfaces), typeKey)

			addServiceName(intrfc, &typeInfo)
			typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
			if intrfc.Enums != nil {
				// Add embedded enums to typeInfo.TypeMap.
				for _, enum := range intrfc.Enums {
					typeKey := enum.TypeKey()
					typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
				}
			}
		}
	}

	// Structs
	if f.Structs != nil && len(f.Structs) > 0 {
		file.DeclaredFidlObjects.Structs = new([]string)
		for _, strct := range f.Structs {
			typeKey := strct.TypeKey()
			*(file.DeclaredFidlObjects.Structs) = append(*(file.DeclaredFidlObjects.Structs), typeKey)
			typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
			if strct.Enums != nil {
				// Add embedded enums to typeInfo.TypeMap.
				for _, enum := range strct.Enums {
					typeKey := enum.TypeKey()
					typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
				}
			}
		}
	}

	// Unions
	if f.Unions != nil && len(f.Unions) > 0 {
		file.DeclaredFidlObjects.Unions = new([]string)
		for _, union := range f.Unions {
			typeKey := union.TypeKey()
			*(file.DeclaredFidlObjects.Unions) = append(*(file.DeclaredFidlObjects.Unions), typeKey)
			typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
		}
	}

	// TopLevelEnums
	if f.Enums != nil && len(f.Enums) > 0 {
		file.DeclaredFidlObjects.TopLevelEnums = new([]string)
		for _, enum := range f.Enums {
			typeKey := enum.TypeKey()
			*(file.DeclaredFidlObjects.TopLevelEnums) = append(*(file.DeclaredFidlObjects.TopLevelEnums), typeKey)
			typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
		}
	}

	// TopLevelConstants
	if f.Constants != nil && len(f.Constants) > 0 {
		file.DeclaredFidlObjects.TopLevelConstants = new([]string)
		for _, constant := range f.Constants {
			*(file.DeclaredFidlObjects.TopLevelConstants) = append(*(file.DeclaredFidlObjects.TopLevelConstants), constant.ValueKey())
		}
	}

	// TODO(rudominer) Do we need the EmbeddedEnums and EmbeddedConstants
	// fields in KeysByType. It seems these fields are not currently being
	// used in fidl_translator.py.

	// SerializedRuntimeTypeInfo
	if emitSerializedRuntimeTypeInfo {
		encoder := bindings.NewEncoder()
		encoder.SetDeterministic(true)
		typeInfo.Encode(encoder)
		byteSlice, _, err := encoder.Data()
		if err != nil {
			panic(fmt.Sprintf("Error while serializing runtimeTypeInfo: %s", err.Error()))
		}
		var compressedBytes bytes.Buffer
		gzipWriter, _ := gzip.NewWriterLevel(&compressedBytes, gzip.BestCompression)
		_, err = gzipWriter.Write(byteSlice)
		if err != nil {
			panic(fmt.Sprintf("Error while gzipping runtimeTypeInfo: %s", err.Error()))
		}
		err = gzipWriter.Close()
		if err != nil {
			panic(fmt.Sprintf("Error while gzipping runtimeTypeInfo: %s", err.Error()))
		}
		byteSlice = compressedBytes.Bytes()
		encoded := base64.StdEncoding.EncodeToString(byteSlice)
		file.SerializedRuntimeTypeInfo = &encoded
	}

	return
}

// addServiceName will add the service name of |intrfc| to the |Services| field of |typeInfo|
// if |intrfc| is a top-level interface, meaning that it has a non-nil service name. In that
// case this method returns true. Otherwise this method will do nothing and return fals.
func addServiceName(intrfc *core.MojomInterface, typeInfo *fidl_types.RuntimeTypeInfo) (isTopLevel bool) {
	isTopLevel = intrfc.ServiceName != nil
	if isTopLevel {
		typeInfo.Services[*intrfc.ServiceName] = intrfc.TypeKey()
	}
	return
}

// translateUserDefinedType translates from a core.UserDefinedType (the pure Go
// representation used by the parser) to a fidl_types.UserDefinedType (the
// Mojo Go representation used for serialization.)
func translateUserDefinedType(t core.UserDefinedType) fidl_types.UserDefinedType {
	switch p := t.(type) {
	case *core.MojomStruct:
		return &fidl_types.UserDefinedTypeStructType{translateFidlStruct(p)}
	case *core.MojomInterface:
		return translateFidlInterface(p)
	case *core.MojomEnum:
		return translateFidlEnum(p)
	case *core.MojomUnion:
		return translateFidlUnion(p)
	default:
		panic(fmt.Sprintf("Unexpected type: %T", t))

	}
}

func translateFidlStruct(s *core.MojomStruct) fidl_types.FidlStruct {
	fidlStruct := fidl_types.FidlStruct{}
	fidlStruct.DeclData = translateDeclarationData(&s.DeclarationData)
	fidlStruct.DeclData.ContainedDeclarations = translateContainedDeclarations(&s.NestedDeclarations)

	for _, field := range s.FieldsInOrdinalOrder() {
		fidlStruct.Fields = append(fidlStruct.Fields, translateStructField(field))
	}

	if emitComputedPackingData {
		versionInfo := s.VersionInfo()
		fidlStruct.VersionInfo = new([]fidl_types.StructVersion)
		for _, version := range versionInfo {
			(*fidlStruct.VersionInfo) = append(*fidlStruct.VersionInfo,
				translateStructVersion(version))
		}
	}

	return fidlStruct
}

func translateStructVersion(v core.StructVersion) fidl_types.StructVersion {
	return fidl_types.StructVersion{
		VersionNumber: v.VersionNumber,
		NumFields:     v.NumFields,
		NumBytes:      v.NumBytes,
	}
}

func translateStructField(f *core.StructField) (field fidl_types.StructField) {
	field.DeclData = translateDeclarationData(&f.DeclarationData)
	field.Type = translateTypeRef(f.FieldType)
	if f.DefaultValue != nil {
		field.DefaultValue = translateDefaultFieldValue(f.DefaultValue)
	}
	if emitComputedPackingData {
		field.Offset = f.Offset()
		field.Bit = int8(f.Bit())
		field.MinVersion = f.MinVersion()
	}
	return
}

func translateDefaultFieldValue(v core.ValueRef) fidl_types.DefaultFieldValue {
	switch v := v.(type) {
	case core.LiteralValue:
		if v.IsDefault() {
			return &fidl_types.DefaultFieldValueDefaultKeyword{fidl_types.DefaultKeyword{}}
		}
		return &fidl_types.DefaultFieldValueValue{translateLiteralValue(v)}
	case *core.UserValueRef:
		return &fidl_types.DefaultFieldValueValue{translateUserValueRef(v)}
	default:
		panic(fmt.Sprintf("Unexpected ValueRef type: %T", v))

	}
}

func translateFidlInterface(intrfc *core.MojomInterface) *fidl_types.UserDefinedTypeInterfaceType {
	fidlInterface := fidl_types.UserDefinedTypeInterfaceType{}

	fidlInterface.Value.DeclData = translateDeclarationData(&intrfc.DeclarationData)
	fidlInterface.Value.DeclData.ContainedDeclarations = translateContainedDeclarations(&intrfc.NestedDeclarations)

	fidlInterface.Value.ServiceName = intrfc.ServiceName

	fidlInterface.Value.Methods = make(map[uint32]fidl_types.FidlMethod)
	for ordinal, method := range intrfc.MethodsByOrdinal {
		fidlInterface.Value.Methods[ordinal] = translateFidlMethod(method)
	}
	if emitComputedPackingData {
		fidlInterface.Value.CurrentVersion = intrfc.Version()
	}

	return &fidlInterface
}

func translateFidlMethod(method *core.MojomMethod) fidl_types.FidlMethod {
	fidlMethod := fidl_types.FidlMethod{}

	// decl_data
	fidlMethod.DeclData = translateDeclarationData(&method.DeclarationData)

	// parameters
	fidlMethod.Parameters = translateFidlStruct(method.Parameters)

	// response_params
	if method.ResponseParameters != nil {
		responseParams := translateFidlStruct(method.ResponseParameters)
		fidlMethod.ResponseParams = &responseParams
	}

	// ordinal
	fidlMethod.Ordinal = method.Ordinal

	if emitComputedPackingData {
		fidlMethod.MinVersion = method.MinVersion()
	}

	return fidlMethod
}

func translateFidlEnum(enum *core.MojomEnum) *fidl_types.UserDefinedTypeEnumType {
	fidlEnum := fidl_types.UserDefinedTypeEnumType{}
	fidlEnum.Value.DeclData = translateDeclarationData(&enum.DeclarationData)
	for _, value := range enum.Values {
		fidlEnum.Value.Values = append(fidlEnum.Value.Values, translateEnumValue(value))
	}
	return &fidlEnum
}

func translateFidlUnion(union *core.MojomUnion) *fidl_types.UserDefinedTypeUnionType {
	fidlUnion := fidl_types.UserDefinedTypeUnionType{}
	fidlUnion.Value.DeclData = translateDeclarationData(&union.DeclarationData)
	for _, field := range union.FieldsInTagOrder() {
		fidlUnion.Value.Fields = append(fidlUnion.Value.Fields,
			translateUnionField(field))
	}
	return &fidlUnion
}

func translateUnionField(unionField *core.UnionField) fidl_types.UnionField {
	outUnionField := fidl_types.UnionField{}
	outUnionField.DeclData = translateDeclarationData(&unionField.DeclarationData)
	outUnionField.Type = translateTypeRef(unionField.FieldType)
	outUnionField.Tag = unionField.Tag
	return outUnionField
}

func translateUserDefinedConstant(t *core.UserDefinedConstant) fidl_types.DeclaredConstant {
	declaredConstant := fidl_types.DeclaredConstant{}
	declaredConstant.Type = translateTypeRef(t.DeclaredType())
	declaredConstant.DeclData = *translateDeclarationData(&t.DeclarationData)
	declaredConstant.Value = translateValueRef(t.ValueRef())
	// We set the |resolved_concrete_value| field only in the case that the |value| field is a ConstantReference.
	// See the comments for this field in fidl_types.core.
	if _, ok := declaredConstant.Value.(*fidl_types.ValueConstantReference); ok {
		declaredConstant.ResolvedConcreteValue = translateConcreteValue(t.ValueRef().ResolvedConcreteValue())
	}

	return declaredConstant
}

func translateEnumValue(v *core.EnumValue) fidl_types.EnumValue {
	enumValue := fidl_types.EnumValue{}
	enumValue.DeclData = translateDeclarationData(&v.DeclarationData)
	if v.ValueRef() != nil {
		enumValue.InitializerValue = translateValueRef(v.ValueRef())
	}
	if !v.IntValueComputed {
		panic(fmt.Sprintf("IntValueComputed is false for %v.", v))
	}
	enumValue.IntValue = v.ComputedIntValue
	return enumValue
}

func translateTypeRef(typeRef core.TypeRef) fidl_types.Type {
	switch t := typeRef.(type) {
	case core.SimpleType:
		return translateSimpleType(t)
	case core.StringType:
		return translateStringType(t)
	case core.HandleTypeRef:
		return translateHandleType(t)
	case core.ArrayTypeRef:
		return translateArrayType(&t)
	case *core.ArrayTypeRef:
		return translateArrayType(t)
	case core.MapTypeRef:
		return translateMapType(&t)
	case *core.MapTypeRef:
		return translateMapType(t)
	case *core.UserTypeRef:
		return translateUserTypeRef(t)
	default:
		panic(fmt.Sprintf("Unexpected TypeRef type %T", t))
	}
}

func translateSimpleType(simpleType core.SimpleType) *fidl_types.TypeSimpleType {
	var value fidl_types.SimpleType
	switch simpleType {
	case core.SimpleTypeBool:
		value = fidl_types.SimpleType_Bool
	case core.SimpleTypeDouble:
		value = fidl_types.SimpleType_Double
	case core.SimpleTypeFloat:
		value = fidl_types.SimpleType_Float
	case core.SimpleTypeInt8:
		value = fidl_types.SimpleType_Int8
	case core.SimpleTypeInt16:
		value = fidl_types.SimpleType_Int16
	case core.SimpleTypeInt32:
		value = fidl_types.SimpleType_Int32
	case core.SimpleTypeInt64:
		value = fidl_types.SimpleType_Int64
	case core.SimpleTypeUInt8:
		value = fidl_types.SimpleType_Uint8
	case core.SimpleTypeUInt16:
		value = fidl_types.SimpleType_Uint16
	case core.SimpleTypeUInt32:
		value = fidl_types.SimpleType_Uint32
	case core.SimpleTypeUInt64:
		value = fidl_types.SimpleType_Uint64
	}
	return &fidl_types.TypeSimpleType{value}
}

func translateStringType(stringType core.StringType) *fidl_types.TypeStringType {
	return &fidl_types.TypeStringType{fidl_types.StringType{stringType.Nullable()}}
}

func translateHandleType(handleType core.HandleTypeRef) *fidl_types.TypeHandleType {
	var kind fidl_types.HandleType_Kind
	switch handleType.HandleKind() {
	case core.HandleKindUnspecified:
		kind = fidl_types.HandleType_Kind_Unspecified
	case core.HandleKindMessagePipe:
		kind = fidl_types.HandleType_Kind_MessagePipe
	case core.HandleKindDataPipeConsumer:
		kind = fidl_types.HandleType_Kind_DataPipeConsumer
	case core.HandleKindDataPipeProducer:
		kind = fidl_types.HandleType_Kind_DataPipeProducer
	case core.HandleKindSharedBuffer:
		kind = fidl_types.HandleType_Kind_SharedBuffer
	}
	return &fidl_types.TypeHandleType{fidl_types.HandleType{handleType.Nullable(), kind}}
}

func translateArrayType(arrayType *core.ArrayTypeRef) *fidl_types.TypeArrayType {
	return &fidl_types.TypeArrayType{fidl_types.ArrayType{
		Nullable:    arrayType.Nullable(),
		FixedLength: int32(arrayType.FixedLength()),
		ElementType: translateTypeRef(arrayType.ElementType())}}
}

func translateMapType(mapType *core.MapTypeRef) *fidl_types.TypeMapType {
	return &fidl_types.TypeMapType{fidl_types.MapType{
		Nullable:  mapType.Nullable(),
		KeyType:   translateTypeRef(mapType.KeyType()),
		ValueType: translateTypeRef(mapType.ValueType())}}
}

func translateUserTypeRef(userType *core.UserTypeRef) *fidl_types.TypeTypeReference {
	typeKey := stringPointer(userType.ResolvedType().TypeKey())
	identifier := stringPointer(userType.Identifier())
	return &fidl_types.TypeTypeReference{fidl_types.TypeReference{
		Nullable:           userType.Nullable(),
		IsInterfaceRequest: userType.IsInterfaceRequest(),
		Identifier:         identifier,
		TypeKey:            typeKey}}
}

func translateValueRef(valueRef core.ValueRef) fidl_types.Value {
	switch valueRef := valueRef.(type) {
	case core.LiteralValue:
		return translateLiteralValue(valueRef)
	case *core.UserValueRef:
		return translateUserValueRef(valueRef)
	default:
		panic(fmt.Sprintf("Unexpected ValueRef type %T", valueRef))
	}
}

func translateConcreteValue(cv core.ConcreteValue) fidl_types.Value {
	switch cv := cv.(type) {
	case core.LiteralValue:
		return translateLiteralValue(cv)
	// NOTE: See the comments at the top of types.go for a discussion of the difference
	// between a value and a value reference. In this function we are translating a
	// value, not a value reference. In the case of a LiteralValue or a
	// BuiltInConstantValue the distinction is immaterial. But in the case of an
	// enum value the distinction is important. Here we are building and returning
	// a synthetic fidl_types.EnumValueReference to represent the enum value.
	// It does not make sense to populate the |identifier| field for because we
	// aren't representing any actual occrence in the .fidl file.
	case *core.EnumValue:
		return &fidl_types.ValueEnumValueReference{fidl_types.EnumValueReference{
			EnumTypeKey:    cv.EnumType().TypeKey(),
			EnumValueIndex: cv.ValueIndex()}}
	case core.BuiltInConstantValue:
		return translateBuiltInConstantValue(cv)
	default:
		panic(fmt.Sprintf("Unexpected ConcreteValue type %T", cv))
	}
}

func translateLiteralValue(v core.LiteralValue) *fidl_types.ValueLiteralValue {
	var lv fidl_types.LiteralValue
	switch v.ValueType() {
	case core.SimpleTypeBool:
		lv = &fidl_types.LiteralValueBoolValue{v.Value().(bool)}
	case core.SimpleTypeDouble:
		lv = &fidl_types.LiteralValueDoubleValue{v.Value().(float64)}
	case core.SimpleTypeFloat:
		lv = &fidl_types.LiteralValueFloatValue{v.Value().(float32)}
	case core.SimpleTypeInt8:
		lv = &fidl_types.LiteralValueInt8Value{v.Value().(int8)}
	case core.SimpleTypeInt16:
		lv = &fidl_types.LiteralValueInt16Value{v.Value().(int16)}
	case core.SimpleTypeInt32:
		lv = &fidl_types.LiteralValueInt32Value{v.Value().(int32)}
	case core.SimpleTypeInt64:
		lv = &fidl_types.LiteralValueInt64Value{v.Value().(int64)}
	case core.SimpleTypeUInt8:
		lv = &fidl_types.LiteralValueUint8Value{v.Value().(uint8)}
	case core.SimpleTypeUInt16:
		lv = &fidl_types.LiteralValueUint16Value{v.Value().(uint16)}
	case core.SimpleTypeUInt32:
		lv = &fidl_types.LiteralValueUint32Value{v.Value().(uint32)}
	case core.SimpleTypeUInt64:
		lv = &fidl_types.LiteralValueUint64Value{v.Value().(uint64)}
	case core.StringLiteralType:
		lv = &fidl_types.LiteralValueStringValue{v.Value().(string)}
	default:
		panic(fmt.Sprintf("Unexpected literal value type %d", v.ValueType()))
	}
	return &fidl_types.ValueLiteralValue{lv}
}

func translateBuiltInConstantValue(t core.BuiltInConstantValue) *fidl_types.ValueBuiltinValue {
	builtInValue := fidl_types.ValueBuiltinValue{}
	switch t {
	case core.FloatInfinity:
		builtInValue.Value = fidl_types.BuiltinConstantValue_FloatInfinity
	case core.FloatNegativeInfinity:
		builtInValue.Value = fidl_types.BuiltinConstantValue_FloatNegativeInfinity
	case core.FloatNAN:
		builtInValue.Value = fidl_types.BuiltinConstantValue_FloatNan
	case core.DoubleInfinity:
		builtInValue.Value = fidl_types.BuiltinConstantValue_DoubleInfinity
	case core.DoubleNegativeInfinity:
		builtInValue.Value = fidl_types.BuiltinConstantValue_DoubleNegativeInfinity
	case core.DoubleNAN:
		builtInValue.Value = fidl_types.BuiltinConstantValue_DoubleNan
	default:
		panic(fmt.Sprintf("Unrecognized BuiltInConstantValue %v", t))
	}
	return &builtInValue
}

func translateUserValueRef(r *core.UserValueRef) fidl_types.Value {
	switch t := r.ResolvedDeclaredValue().(type) {
	case core.BuiltInConstantValue:
		return translateBuiltInConstantValue(t)
	case *core.UserDefinedConstant:
		return &fidl_types.ValueConstantReference{fidl_types.ConstantReference{
			Identifier:  r.Identifier(),
			ConstantKey: t.ValueKey()}}
	case *core.EnumValue:
		return &fidl_types.ValueEnumValueReference{fidl_types.EnumValueReference{
			Identifier:     r.Identifier(),
			EnumTypeKey:    t.EnumType().TypeKey(),
			EnumValueIndex: t.ValueIndex()}}
	default:
		panic(fmt.Sprintf("Unrecognized UserDefinedValueType %T", r.ResolvedDeclaredValue()))
	}
}

func translateDeclarationData(d *core.DeclarationData) *fidl_types.DeclarationData {
	declData := fidl_types.DeclarationData{}

	// attributes field
	if d.Attributes() != nil {
		declData.Attributes = new([]fidl_types.Attribute)
		for _, attr := range d.Attributes().List {
			*(declData.Attributes) = append(*(declData.Attributes), translateFidlAttribute(&attr))
		}
	}

	// min_version field
	// TODO(rudominer) Eliminate the min_version field from struct DeclarationData

	// short_name field
	declData.ShortName = stringPointer(d.SimpleName())

	// full_identifier field
	switch declaredObject := d.DeclaredObject().(type) {
	// We do not serialize the fully-qualified-name for objects that only exist
	// as children of their container objects and are not independently
	// referenceable.
	case *core.StructField, *core.MojomMethod, *core.UnionField:
	case *core.MojomStruct:
		switch declaredObject.StructType() {
		case core.StructTypeRegular:
			declData.FullIdentifier = stringPointer(d.FullyQualifiedName())
		}
	default:
		declData.FullIdentifier = stringPointer(d.FullyQualifiedName())
	}

	// declared_ordinal field
	if d.DeclaredOrdinal() < 0 {
		declData.DeclaredOrdinal = -1
	} else {
		declData.DeclaredOrdinal = int32(d.DeclaredOrdinal())
	}

	// declaration_order
	if d.LexicalPosition() < 0 {
		declData.DeclarationOrder = -1
	} else {
		declData.DeclarationOrder = d.LexicalPosition()
	}

	// container_type_key
	containingType := d.ContainingType()
	if containingType != nil {
		switch d.DeclaredObject().(type) {
		// We do not serialize the |container_type_key| field for objects that only exist
		// as children of their container objects and are not independently
		// referenceable.
		case *core.StructField, *core.MojomMethod:
			// We do not serialize the |container_type_key| field for an EnumValue
			// because the EnumValue already has the type_key of the Enum
			// in a different field.
		case *core.EnumValue:
		default:
			declData.ContainerTypeKey = stringPointer(containingType.TypeKey())
		}
	}

	// source_file_info
	declData.SourceFileInfo = new(fidl_types.SourceFileInfo)
	declData.SourceFileInfo.FileName = d.OwningFile().CanonicalFileName
	if emitLineAndColumnNumbers {
		declData.SourceFileInfo.LineNumber = d.LineNumber()
		declData.SourceFileInfo.ColumnNumber = d.ColumnNumber()
	}
	return &declData
}

// Returns nil if there are no contained declarations
func translateContainedDeclarations(container *core.NestedDeclarations) *fidl_types.ContainedDeclarations {
	if container.Enums == nil && container.Constants == nil {
		return nil
	}
	declarations := fidl_types.ContainedDeclarations{}
	if container.Enums != nil {
		declarations.Enums = new([]string)
		for _, enum := range container.Enums {
			*declarations.Enums = append(*declarations.Enums, enum.TypeKey())
		}
	}
	if container.Constants != nil {
		declarations.Constants = new([]string)
		for _, constant := range container.Constants {
			*declarations.Constants = append(*declarations.Constants, constant.ValueKey())
		}
	}
	return &declarations
}

func translateFidlAttribute(a *core.MojomAttribute) (attribute fidl_types.Attribute) {
	return fidl_types.Attribute{a.Key, translateLiteralValue(a.Value).Value}
}

// stringPointer is a convenience function for creating a pointer to a string whose value
// is the specified string. It may be used in situations where the compiler will
// not allow you to take the address of a string value directly, such as the
// return value of a function. It is necessary to create pointers to strings because
// that is how the Fidl type |string?| (i.e. nullable string) is represented in
// in the Fidl Go bindings.
func stringPointer(s string) *string {
	return &s
}
