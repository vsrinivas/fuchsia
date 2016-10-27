// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serialization

import (
	"bytes"
	"compress/gzip"
	"encoding/base64"
	"fmt"
	"mojo/public/go/bindings"
	"mojom/generated/mojom_files"
	"mojom/generated/mojom_types"
	"mojom/mojom_tool/mojom"
	myfmt "mojom/mojom_tool/third_party/golang/src/fmt"
)

//////////////////////////////////////////////////
/// Mojom Descriptor Serialization
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

// Serialize serializes the MojomDescriptor into a binary form that is passed to the
// backend of the compiler in order to invoke the code generators.
// To do this we use Mojo serialization.
// If |debug| is true we also return a human-readable representation
// of the serialized mojom_types.FileGraph.
// This function is not thread safe.
func Serialize(d *mojom.MojomDescriptor, debug bool) (bytes []byte, debugString string, err error) {
	return serialize(d, debug, true, true, true)
}

// serialize() is a package-private version of the public method Serialize().
// It is intended for use in tests because it allows setting of the variables
// emitLineAndColumnNumbers, emitComputedPackingData and emitSerializedRuntimeTypeInfo.
// This function is not thread safe because it sets and accesses these global
// variables.
func serialize(d *mojom.MojomDescriptor, debug,
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

// translateDescriptor translates from a mojom.MojomDescriptor (the pure Go
// representation used by the parser) to a mojom_files.MojomFileGraph (the
// Mojo Go representation used for serialization.)
func translateDescriptor(d *mojom.MojomDescriptor) *mojom_files.MojomFileGraph {
	fileGraph := mojom_files.MojomFileGraph{}

	// Add |resolved_types| field.
	fileGraph.ResolvedTypes = make(map[string]mojom_types.UserDefinedType)
	for key, userDefinedType := range d.TypesByKey {
		fileGraph.ResolvedTypes[key] = translateUserDefinedType(userDefinedType)
	}

	// Add |resolved_constants| field.
	fileGraph.ResolvedConstants = make(map[string]mojom_types.DeclaredConstant)
	for key, userDefinedValue := range d.ValuesByKey {
		switch c := userDefinedValue.(type) {
		// Note that our representation of values in mojom_types.mojom is a little different than our
		// pure Go representation. In the latter we use value keys to refer to both constants and
		// enum values but in the former we only use value keys to refer to constants. Enum values
		// are stored as part of their enum types and they are are referred to  not directly using
		// value keyes but rather via the type key of their enum and an index into the |values| array
		// of that enum. For this reason we are only looking for the constants here and ignoring the
		// enum values. Thos will get translated when the
		case *mojom.UserDefinedConstant:
			fileGraph.ResolvedConstants[key] = translateUserDefinedConstant(c)
		}
	}

	// Add |files| field.
	fileGraph.Files = make(map[string]mojom_files.MojomFile)
	for name, file := range d.MojomFilesByName {
		fileGraph.Files[name] = translateMojomFile(file, &fileGraph)
	}

	return &fileGraph
}

// translateMojomFile translates from a mojom.MojomFile (the pure Go
// representation used by the parser) to a mojom_files.MojomFile (the
// Mojo Go representation used for serialization.)
func translateMojomFile(f *mojom.MojomFile, fileGraph *mojom_files.MojomFileGraph) (file mojom_files.MojomFile) {
	// file_name field
	file.FileName = f.CanonicalFileName

	// specified_file_name_field
	file.SpecifiedFileName = &f.SpecifiedFileName

	// module_namespace field
	file.ModuleNamespace = &f.ModuleNamespace.Identifier

	// attributes field
	if f.Attributes != nil {
		file.Attributes = new([]mojom_types.Attribute)
		for _, attr := range f.Attributes.List {
			*(file.Attributes) = append(*(file.Attributes), translateMojomAttribute(&attr))
		}
	}

	// imports field
	if len(f.Imports) > 0 {
		file.Imports = new([]string)
		for _, importName := range f.Imports {
			// We support serializing a MojomFileGraph that has not had import names
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
	// of the MojomFile.
	typeInfo := mojom_types.RuntimeTypeInfo{}
	typeInfo.Services = make(map[string]string)
	typeInfo.TypeMap = make(map[string]mojom_types.UserDefinedType)

	// We populate the declared_mojom_objects field
	// and simultaneously we populate typeInfo.TypeMap.

	// Interfaces
	if f.Interfaces != nil && len(f.Interfaces) > 0 {
		file.DeclaredMojomObjects.Interfaces = new([]string)
		for _, intrfc := range f.Interfaces {
			typeKey := intrfc.TypeKey()
			*(file.DeclaredMojomObjects.Interfaces) = append(*(file.DeclaredMojomObjects.Interfaces), typeKey)

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
		file.DeclaredMojomObjects.Structs = new([]string)
		for _, strct := range f.Structs {
			typeKey := strct.TypeKey()
			*(file.DeclaredMojomObjects.Structs) = append(*(file.DeclaredMojomObjects.Structs), typeKey)
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
		file.DeclaredMojomObjects.Unions = new([]string)
		for _, union := range f.Unions {
			typeKey := union.TypeKey()
			*(file.DeclaredMojomObjects.Unions) = append(*(file.DeclaredMojomObjects.Unions), typeKey)
			typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
		}
	}

	// TopLevelEnums
	if f.Enums != nil && len(f.Enums) > 0 {
		file.DeclaredMojomObjects.TopLevelEnums = new([]string)
		for _, enum := range f.Enums {
			typeKey := enum.TypeKey()
			*(file.DeclaredMojomObjects.TopLevelEnums) = append(*(file.DeclaredMojomObjects.TopLevelEnums), typeKey)
			typeInfo.TypeMap[typeKey] = fileGraph.ResolvedTypes[typeKey]
		}
	}

	// TopLevelConstants
	if f.Constants != nil && len(f.Constants) > 0 {
		file.DeclaredMojomObjects.TopLevelConstants = new([]string)
		for _, constant := range f.Constants {
			*(file.DeclaredMojomObjects.TopLevelConstants) = append(*(file.DeclaredMojomObjects.TopLevelConstants), constant.ValueKey())
		}
	}

	// TODO(rudominer) Do we need the EmbeddedEnums and EmbeddedConstants
	// fields in KeysByType. It seems these fields are not currently being
	// used in mojom_translator.py.

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
func addServiceName(intrfc *mojom.MojomInterface, typeInfo *mojom_types.RuntimeTypeInfo) (isTopLevel bool) {
	isTopLevel = intrfc.ServiceName != nil
	if isTopLevel {
		typeInfo.Services[*intrfc.ServiceName] = intrfc.TypeKey()
	}
	return
}

// translateUserDefinedType translates from a mojom.UserDefinedType (the pure Go
// representation used by the parser) to a mojom_types.UserDefinedType (the
// Mojo Go representation used for serialization.)
func translateUserDefinedType(t mojom.UserDefinedType) mojom_types.UserDefinedType {
	switch p := t.(type) {
	case *mojom.MojomStruct:
		return &mojom_types.UserDefinedTypeStructType{translateMojomStruct(p)}
	case *mojom.MojomInterface:
		return translateMojomInterface(p)
	case *mojom.MojomEnum:
		return translateMojomEnum(p)
	case *mojom.MojomUnion:
		return translateMojomUnion(p)
	default:
		panic(fmt.Sprintf("Unexpected type: %T", t))

	}
}

func translateMojomStruct(s *mojom.MojomStruct) mojom_types.MojomStruct {
	mojomStruct := mojom_types.MojomStruct{}
	mojomStruct.DeclData = translateDeclarationData(&s.DeclarationData)
	mojomStruct.DeclData.ContainedDeclarations = translateContainedDeclarations(&s.NestedDeclarations)

	for _, field := range s.FieldsInOrdinalOrder() {
		mojomStruct.Fields = append(mojomStruct.Fields, translateStructField(field))
	}

	if emitComputedPackingData {
		versionInfo := s.VersionInfo()
		mojomStruct.VersionInfo = new([]mojom_types.StructVersion)
		for _, version := range versionInfo {
			(*mojomStruct.VersionInfo) = append(*mojomStruct.VersionInfo,
				translateStructVersion(version))
		}
	}

	return mojomStruct
}

func translateStructVersion(v mojom.StructVersion) mojom_types.StructVersion {
	return mojom_types.StructVersion{
		VersionNumber: v.VersionNumber,
		NumFields:     v.NumFields,
		NumBytes:      v.NumBytes,
	}
}

func translateStructField(f *mojom.StructField) (field mojom_types.StructField) {
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

func translateDefaultFieldValue(v mojom.ValueRef) mojom_types.DefaultFieldValue {
	switch v := v.(type) {
	case mojom.LiteralValue:
		if v.IsDefault() {
			return &mojom_types.DefaultFieldValueDefaultKeyword{mojom_types.DefaultKeyword{}}
		}
		return &mojom_types.DefaultFieldValueValue{translateLiteralValue(v)}
	case *mojom.UserValueRef:
		return &mojom_types.DefaultFieldValueValue{translateUserValueRef(v)}
	default:
		panic(fmt.Sprintf("Unexpected ValueRef type: %T", v))

	}
}

func translateMojomInterface(intrfc *mojom.MojomInterface) *mojom_types.UserDefinedTypeInterfaceType {
	mojomInterface := mojom_types.UserDefinedTypeInterfaceType{}

	mojomInterface.Value.DeclData = translateDeclarationData(&intrfc.DeclarationData)
	mojomInterface.Value.DeclData.ContainedDeclarations = translateContainedDeclarations(&intrfc.NestedDeclarations)

	mojomInterface.Value.ServiceName = intrfc.ServiceName

	mojomInterface.Value.Methods = make(map[uint32]mojom_types.MojomMethod)
	for ordinal, method := range intrfc.MethodsByOrdinal {
		mojomInterface.Value.Methods[ordinal] = translateMojomMethod(method)
	}
	if emitComputedPackingData {
		mojomInterface.Value.CurrentVersion = intrfc.Version()
	}

	return &mojomInterface
}

func translateMojomMethod(method *mojom.MojomMethod) mojom_types.MojomMethod {
	mojomMethod := mojom_types.MojomMethod{}

	// decl_data
	mojomMethod.DeclData = translateDeclarationData(&method.DeclarationData)

	// parameters
	mojomMethod.Parameters = translateMojomStruct(method.Parameters)

	// response_params
	if method.ResponseParameters != nil {
		responseParams := translateMojomStruct(method.ResponseParameters)
		mojomMethod.ResponseParams = &responseParams
	}

	// ordinal
	mojomMethod.Ordinal = method.Ordinal

	if emitComputedPackingData {
		mojomMethod.MinVersion = method.MinVersion()
	}

	return mojomMethod
}

func translateMojomEnum(enum *mojom.MojomEnum) *mojom_types.UserDefinedTypeEnumType {
	mojomEnum := mojom_types.UserDefinedTypeEnumType{}
	mojomEnum.Value.DeclData = translateDeclarationData(&enum.DeclarationData)
	for _, value := range enum.Values {
		mojomEnum.Value.Values = append(mojomEnum.Value.Values, translateEnumValue(value))
	}
	return &mojomEnum
}

func translateMojomUnion(union *mojom.MojomUnion) *mojom_types.UserDefinedTypeUnionType {
	mojomUnion := mojom_types.UserDefinedTypeUnionType{}
	mojomUnion.Value.DeclData = translateDeclarationData(&union.DeclarationData)
	for _, field := range union.FieldsInTagOrder() {
		mojomUnion.Value.Fields = append(mojomUnion.Value.Fields,
			translateUnionField(field))
	}
	return &mojomUnion
}

func translateUnionField(unionField *mojom.UnionField) mojom_types.UnionField {
	outUnionField := mojom_types.UnionField{}
	outUnionField.DeclData = translateDeclarationData(&unionField.DeclarationData)
	outUnionField.Type = translateTypeRef(unionField.FieldType)
	outUnionField.Tag = unionField.Tag
	return outUnionField
}

func translateUserDefinedConstant(t *mojom.UserDefinedConstant) mojom_types.DeclaredConstant {
	declaredConstant := mojom_types.DeclaredConstant{}
	declaredConstant.Type = translateTypeRef(t.DeclaredType())
	declaredConstant.DeclData = *translateDeclarationData(&t.DeclarationData)
	declaredConstant.Value = translateValueRef(t.ValueRef())
	// We set the |resolved_concrete_value| field only in the case that the |value| field is a ConstantReference.
	// See the comments for this field in mojom_types.mojom.
	if _, ok := declaredConstant.Value.(*mojom_types.ValueConstantReference); ok {
		declaredConstant.ResolvedConcreteValue = translateConcreteValue(t.ValueRef().ResolvedConcreteValue())
	}

	return declaredConstant
}

func translateEnumValue(v *mojom.EnumValue) mojom_types.EnumValue {
	enumValue := mojom_types.EnumValue{}
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

func translateTypeRef(typeRef mojom.TypeRef) mojom_types.Type {
	switch t := typeRef.(type) {
	case mojom.SimpleType:
		return translateSimpleType(t)
	case mojom.StringType:
		return translateStringType(t)
	case mojom.HandleTypeRef:
		return translateHandleType(t)
	case mojom.ArrayTypeRef:
		return translateArrayType(&t)
	case *mojom.ArrayTypeRef:
		return translateArrayType(t)
	case mojom.MapTypeRef:
		return translateMapType(&t)
	case *mojom.MapTypeRef:
		return translateMapType(t)
	case *mojom.UserTypeRef:
		return translateUserTypeRef(t)
	default:
		panic(fmt.Sprintf("Unexpected TypeRef type %T", t))
	}
}

func translateSimpleType(simpleType mojom.SimpleType) *mojom_types.TypeSimpleType {
	var value mojom_types.SimpleType
	switch simpleType {
	case mojom.SimpleTypeBool:
		value = mojom_types.SimpleType_Bool
	case mojom.SimpleTypeDouble:
		value = mojom_types.SimpleType_Double
	case mojom.SimpleTypeFloat:
		value = mojom_types.SimpleType_Float
	case mojom.SimpleTypeInt8:
		value = mojom_types.SimpleType_Int8
	case mojom.SimpleTypeInt16:
		value = mojom_types.SimpleType_Int16
	case mojom.SimpleTypeInt32:
		value = mojom_types.SimpleType_Int32
	case mojom.SimpleTypeInt64:
		value = mojom_types.SimpleType_Int64
	case mojom.SimpleTypeUInt8:
		value = mojom_types.SimpleType_Uint8
	case mojom.SimpleTypeUInt16:
		value = mojom_types.SimpleType_Uint16
	case mojom.SimpleTypeUInt32:
		value = mojom_types.SimpleType_Uint32
	case mojom.SimpleTypeUInt64:
		value = mojom_types.SimpleType_Uint64
	}
	return &mojom_types.TypeSimpleType{value}
}

func translateStringType(stringType mojom.StringType) *mojom_types.TypeStringType {
	return &mojom_types.TypeStringType{mojom_types.StringType{stringType.Nullable()}}
}

func translateHandleType(handleType mojom.HandleTypeRef) *mojom_types.TypeHandleType {
	var kind mojom_types.HandleType_Kind
	switch handleType.HandleKind() {
	case mojom.HandleKindUnspecified:
		kind = mojom_types.HandleType_Kind_Unspecified
	case mojom.HandleKindMessagePipe:
		kind = mojom_types.HandleType_Kind_MessagePipe
	case mojom.HandleKindDataPipeConsumer:
		kind = mojom_types.HandleType_Kind_DataPipeConsumer
	case mojom.HandleKindDataPipeProducer:
		kind = mojom_types.HandleType_Kind_DataPipeProducer
	case mojom.HandleKindSharedBuffer:
		kind = mojom_types.HandleType_Kind_SharedBuffer
	}
	return &mojom_types.TypeHandleType{mojom_types.HandleType{handleType.Nullable(), kind}}
}

func translateArrayType(arrayType *mojom.ArrayTypeRef) *mojom_types.TypeArrayType {
	return &mojom_types.TypeArrayType{mojom_types.ArrayType{
		Nullable:    arrayType.Nullable(),
		FixedLength: int32(arrayType.FixedLength()),
		ElementType: translateTypeRef(arrayType.ElementType())}}
}

func translateMapType(mapType *mojom.MapTypeRef) *mojom_types.TypeMapType {
	return &mojom_types.TypeMapType{mojom_types.MapType{
		Nullable:  mapType.Nullable(),
		KeyType:   translateTypeRef(mapType.KeyType()),
		ValueType: translateTypeRef(mapType.ValueType())}}
}

func translateUserTypeRef(userType *mojom.UserTypeRef) *mojom_types.TypeTypeReference {
	typeKey := stringPointer(userType.ResolvedType().TypeKey())
	identifier := stringPointer(userType.Identifier())
	return &mojom_types.TypeTypeReference{mojom_types.TypeReference{
		Nullable:           userType.Nullable(),
		IsInterfaceRequest: userType.IsInterfaceRequest(),
		Identifier:         identifier,
		TypeKey:            typeKey}}
}

func translateValueRef(valueRef mojom.ValueRef) mojom_types.Value {
	switch valueRef := valueRef.(type) {
	case mojom.LiteralValue:
		return translateLiteralValue(valueRef)
	case *mojom.UserValueRef:
		return translateUserValueRef(valueRef)
	default:
		panic(fmt.Sprintf("Unexpected ValueRef type %T", valueRef))
	}
}

func translateConcreteValue(cv mojom.ConcreteValue) mojom_types.Value {
	switch cv := cv.(type) {
	case mojom.LiteralValue:
		return translateLiteralValue(cv)
	// NOTE: See the comments at the top of types.go for a discussion of the difference
	// between a value and a value reference. In this function we are translating a
	// value, not a value reference. In the case of a LiteralValue or a
	// BuiltInConstantValue the distinction is immaterial. But in the case of an
	// enum value the distinction is important. Here we are building and returning
	// a synthetic mojom_types.EnumValueReference to represent the enum value.
	// It does not make sense to populate the |identifier| field for because we
	// aren't representing any actual occrence in the .mojom file.
	case *mojom.EnumValue:
		return &mojom_types.ValueEnumValueReference{mojom_types.EnumValueReference{
			EnumTypeKey:    cv.EnumType().TypeKey(),
			EnumValueIndex: cv.ValueIndex()}}
	case mojom.BuiltInConstantValue:
		return translateBuiltInConstantValue(cv)
	default:
		panic(fmt.Sprintf("Unexpected ConcreteValue type %T", cv))
	}
}

func translateLiteralValue(v mojom.LiteralValue) *mojom_types.ValueLiteralValue {
	var lv mojom_types.LiteralValue
	switch v.ValueType() {
	case mojom.SimpleTypeBool:
		lv = &mojom_types.LiteralValueBoolValue{v.Value().(bool)}
	case mojom.SimpleTypeDouble:
		lv = &mojom_types.LiteralValueDoubleValue{v.Value().(float64)}
	case mojom.SimpleTypeFloat:
		lv = &mojom_types.LiteralValueFloatValue{v.Value().(float32)}
	case mojom.SimpleTypeInt8:
		lv = &mojom_types.LiteralValueInt8Value{v.Value().(int8)}
	case mojom.SimpleTypeInt16:
		lv = &mojom_types.LiteralValueInt16Value{v.Value().(int16)}
	case mojom.SimpleTypeInt32:
		lv = &mojom_types.LiteralValueInt32Value{v.Value().(int32)}
	case mojom.SimpleTypeInt64:
		lv = &mojom_types.LiteralValueInt64Value{v.Value().(int64)}
	case mojom.SimpleTypeUInt8:
		lv = &mojom_types.LiteralValueUint8Value{v.Value().(uint8)}
	case mojom.SimpleTypeUInt16:
		lv = &mojom_types.LiteralValueUint16Value{v.Value().(uint16)}
	case mojom.SimpleTypeUInt32:
		lv = &mojom_types.LiteralValueUint32Value{v.Value().(uint32)}
	case mojom.SimpleTypeUInt64:
		lv = &mojom_types.LiteralValueUint64Value{v.Value().(uint64)}
	case mojom.StringLiteralType:
		lv = &mojom_types.LiteralValueStringValue{v.Value().(string)}
	default:
		panic(fmt.Sprintf("Unexpected literal value type %d", v.ValueType()))
	}
	return &mojom_types.ValueLiteralValue{lv}
}

func translateBuiltInConstantValue(t mojom.BuiltInConstantValue) *mojom_types.ValueBuiltinValue {
	builtInValue := mojom_types.ValueBuiltinValue{}
	switch t {
	case mojom.FloatInfinity:
		builtInValue.Value = mojom_types.BuiltinConstantValue_FloatInfinity
	case mojom.FloatNegativeInfinity:
		builtInValue.Value = mojom_types.BuiltinConstantValue_FloatNegativeInfinity
	case mojom.FloatNAN:
		builtInValue.Value = mojom_types.BuiltinConstantValue_FloatNan
	case mojom.DoubleInfinity:
		builtInValue.Value = mojom_types.BuiltinConstantValue_DoubleInfinity
	case mojom.DoubleNegativeInfinity:
		builtInValue.Value = mojom_types.BuiltinConstantValue_DoubleNegativeInfinity
	case mojom.DoubleNAN:
		builtInValue.Value = mojom_types.BuiltinConstantValue_DoubleNan
	default:
		panic(fmt.Sprintf("Unrecognized BuiltInConstantValue %v", t))
	}
	return &builtInValue
}

func translateUserValueRef(r *mojom.UserValueRef) mojom_types.Value {
	switch t := r.ResolvedDeclaredValue().(type) {
	case mojom.BuiltInConstantValue:
		return translateBuiltInConstantValue(t)
	case *mojom.UserDefinedConstant:
		return &mojom_types.ValueConstantReference{mojom_types.ConstantReference{
			Identifier:  r.Identifier(),
			ConstantKey: t.ValueKey()}}
	case *mojom.EnumValue:
		return &mojom_types.ValueEnumValueReference{mojom_types.EnumValueReference{
			Identifier:     r.Identifier(),
			EnumTypeKey:    t.EnumType().TypeKey(),
			EnumValueIndex: t.ValueIndex()}}
	default:
		panic(fmt.Sprintf("Unrecognized UserDefinedValueType %T", r.ResolvedDeclaredValue()))
	}
}

func translateDeclarationData(d *mojom.DeclarationData) *mojom_types.DeclarationData {
	declData := mojom_types.DeclarationData{}

	// attributes field
	if d.Attributes() != nil {
		declData.Attributes = new([]mojom_types.Attribute)
		for _, attr := range d.Attributes().List {
			*(declData.Attributes) = append(*(declData.Attributes), translateMojomAttribute(&attr))
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
	case *mojom.StructField, *mojom.MojomMethod, *mojom.UnionField:
	case *mojom.MojomStruct:
		switch declaredObject.StructType() {
		case mojom.StructTypeRegular:
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
		case *mojom.StructField, *mojom.MojomMethod:
			// We do not serialize the |container_type_key| field for an EnumValue
			// because the EnumValue already has the type_key of the Enum
			// in a different field.
		case *mojom.EnumValue:
		default:
			declData.ContainerTypeKey = stringPointer(containingType.TypeKey())
		}
	}

	// source_file_info
	declData.SourceFileInfo = new(mojom_types.SourceFileInfo)
	declData.SourceFileInfo.FileName = d.OwningFile().CanonicalFileName
	if emitLineAndColumnNumbers {
		declData.SourceFileInfo.LineNumber = d.LineNumber()
		declData.SourceFileInfo.ColumnNumber = d.ColumnNumber()
	}
	return &declData
}

// Returns nil if there are no contained declarations
func translateContainedDeclarations(container *mojom.NestedDeclarations) *mojom_types.ContainedDeclarations {
	if container.Enums == nil && container.Constants == nil {
		return nil
	}
	declarations := mojom_types.ContainedDeclarations{}
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

func translateMojomAttribute(a *mojom.MojomAttribute) (attribute mojom_types.Attribute) {
	return mojom_types.Attribute{a.Key, translateLiteralValue(a.Value).Value}
}

// stringPointer is a convenience function for creating a pointer to a string whose value
// is the specified string. It may be used in situations where the compiler will
// not allow you to take the address of a string value directly, such as the
// return value of a function. It is necessary to create pointers to strings because
// that is how the Mojom type |string?| (i.e. nullable string) is represented in
// in the Mojom Go bindings.
func stringPointer(s string) *string {
	return &s
}
