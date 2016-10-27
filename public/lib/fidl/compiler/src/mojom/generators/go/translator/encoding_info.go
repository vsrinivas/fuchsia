// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"fmt"

	"mojom/generated/mojom_types"
)

// This file implements the methods which create the EncodingInfo (see mojom_file.go)
// for each type.
func (t *translator) encodingInfo(mojomType mojom_types.Type) (info EncodingInfo) {
	return t.encodingInfoNested(mojomType, 0)
}

func (t *translator) encodingInfoNested(mojomType mojom_types.Type, level int) (info EncodingInfo) {
	switch m := mojomType.(type) {
	default:
		panic("This should never happen.")
	case *mojom_types.TypeSimpleType:
		info = t.simpleTypeEncodingInfo(m.Value)
	case *mojom_types.TypeStringType:
		info = t.stringTypeEncodingInfo(m.Value)
	case *mojom_types.TypeHandleType:
		info = t.handleTypeEncodingInfo(m.Value)
	case *mojom_types.TypeArrayType:
		info = t.arrayTypeEncodingInfo(m.Value, level)
	case *mojom_types.TypeMapType:
		info = t.mapTypeEncodingInfo(m.Value, level)
	case *mojom_types.TypeTypeReference:
		info = t.typeRefEncodingInfo(m.Value)
	}
	info.setGoType(t.translateType(mojomType))
	return info
}

func (t *translator) simpleTypeEncodingInfo(mojomType mojom_types.SimpleType) (info *simpleTypeEncodingInfo) {
	info = new(simpleTypeEncodingInfo)
	var typeSuffix string
	var bitSize uint32
	switch mojomType {
	default:
		panic("Not a valid SimpleType.")
	case mojom_types.SimpleType_Bool:
		typeSuffix = "Bool"
		bitSize = 1
	case mojom_types.SimpleType_Double:
		typeSuffix = "Float64"
		bitSize = 64
	case mojom_types.SimpleType_Float:
		typeSuffix = "Float32"
		bitSize = 32
	case mojom_types.SimpleType_Int8:
		typeSuffix = "Int8"
		bitSize = 8
	case mojom_types.SimpleType_Int16:
		typeSuffix = "Int16"
		bitSize = 16
	case mojom_types.SimpleType_Int32:
		typeSuffix = "Int32"
		bitSize = 32
	case mojom_types.SimpleType_Int64:
		typeSuffix = "Int64"
		bitSize = 64
	case mojom_types.SimpleType_Uint8:
		typeSuffix = "Uint8"
		bitSize = 8
	case mojom_types.SimpleType_Uint16:
		typeSuffix = "Uint16"
		bitSize = 16
	case mojom_types.SimpleType_Uint32:
		typeSuffix = "Uint32"
		bitSize = 32
	case mojom_types.SimpleType_Uint64:
		typeSuffix = "Uint64"
		bitSize = 64
	}
	info.writeFunction = "Write" + typeSuffix
	info.readFunction = "Read" + typeSuffix
	info.bitSize = bitSize
	return info
}

func (t *translator) stringTypeEncodingInfo(mojomType mojom_types.StringType) (info *stringTypeEncodingInfo) {
	info = new(stringTypeEncodingInfo)
	info.nullable = mojomType.Nullable
	return info
}

func (t *translator) handleTypeEncodingInfo(mojomType mojom_types.HandleType) (info *handleTypeEncodingInfo) {
	info = new(handleTypeEncodingInfo)
	info.nullable = mojomType.Nullable
	switch mojomType.Kind {
	case mojom_types.HandleType_Kind_Unspecified:
		info.readFunction = "ReadHandle"
	case mojom_types.HandleType_Kind_MessagePipe:
		info.readFunction = "ReadMessagePipeHandle"
	case mojom_types.HandleType_Kind_DataPipeConsumer:
		info.readFunction = "ReadConsumerHandle"
	case mojom_types.HandleType_Kind_DataPipeProducer:
		info.readFunction = "ReadProducerHandle"
	case mojom_types.HandleType_Kind_SharedBuffer:
		info.readFunction = "ReadSharedBufferHandle"
	}
	return info
}

func (t *translator) arrayTypeEncodingInfo(mojomType mojom_types.ArrayType, level int) (info *arrayTypeEncodingInfo) {
	info = new(arrayTypeEncodingInfo)
	info.fixedSize = mojomType.FixedLength
	info.nullable = mojomType.Nullable
	info.elementEncodingInfo = t.encodingInfoNested(mojomType.ElementType, level+1)
	info.elementEncodingInfo.setIdentifier(fmt.Sprintf("elem%d", level))
	return info
}

func (t *translator) mapTypeEncodingInfo(mojomType mojom_types.MapType, level int) (info *mapTypeEncodingInfo) {
	info = new(mapTypeEncodingInfo)
	info.nullable = mojomType.Nullable

	keyEncodingInfo := new(arrayTypeEncodingInfo)
	info.keyEncodingInfo = keyEncodingInfo
	keyEncodingInfo.fixedSize = -1
	keyEncodingInfo.elementEncodingInfo = t.encodingInfoNested(mojomType.KeyType, level+1)
	keyEncodingInfo.setIdentifier(fmt.Sprintf("keys%d", level))
	keyEncodingInfo.setGoType(fmt.Sprintf("[]%v", keyEncodingInfo.elementEncodingInfo.GoType()))
	keyEncodingInfo.elementEncodingInfo.setIdentifier(fmt.Sprintf("elem%d", level))

	valueEncodingInfo := new(arrayTypeEncodingInfo)
	info.valueEncodingInfo = valueEncodingInfo
	valueEncodingInfo.fixedSize = -1
	valueEncodingInfo.elementEncodingInfo = t.encodingInfoNested(mojomType.ValueType, level+1)
	valueEncodingInfo.setIdentifier(fmt.Sprintf("values%d", level))
	valueEncodingInfo.setGoType(fmt.Sprintf("[]%v", valueEncodingInfo.elementEncodingInfo.GoType()))
	valueEncodingInfo.elementEncodingInfo.setIdentifier(fmt.Sprintf("elem%d", level))

	return info
}

func (t *translator) typeRefEncodingInfo(typeRef mojom_types.TypeReference) (info EncodingInfo) {
	mojomType := t.GetUserDefinedType(*typeRef.TypeKey)
	switch mojomType.(type) {
	default:
		panic("Unsupported type. This should never happen.")
	case *mojom_types.UserDefinedTypeStructType:
		info = new(structTypeEncodingInfo)
	case *mojom_types.UserDefinedTypeUnionType:
		info = new(unionTypeEncodingInfo)
	case *mojom_types.UserDefinedTypeEnumType:
		info = new(enumTypeEncodingInfo)
	case *mojom_types.UserDefinedTypeInterfaceType:
		info = new(interfaceTypeEncodingInfo)
		info.(*interfaceTypeEncodingInfo).interfaceRequest = typeRef.IsInterfaceRequest
	}
	if typeRef.Nullable {
		info.setNullable(true)
	}
	return info
}
