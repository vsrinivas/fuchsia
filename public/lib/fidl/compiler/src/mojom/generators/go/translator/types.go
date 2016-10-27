// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"fmt"

	"mojom/generated/mojom_types"
)

func (t *translator) translateType(mojomType mojom_types.Type) (goType string) {
	switch m := mojomType.(type) {
	default:
		panic("Not implemented yet!")
	case *mojom_types.TypeSimpleType:
		goType = t.translateSimpleType(m.Value)
	case *mojom_types.TypeStringType:
		goType = t.translateStringType(m.Value)
	case *mojom_types.TypeHandleType:
		goType = t.translateHandleType(m.Value)
	case *mojom_types.TypeTypeReference:
		goType = t.translateTypeReference(m.Value)
	case *mojom_types.TypeArrayType:
		goType = t.translateArrayType(m.Value)
	case *mojom_types.TypeMapType:
		goType = t.translateMapType(m.Value)
	}
	return
}

func (t *translator) translateSimpleType(mojomType mojom_types.SimpleType) string {
	switch mojomType {
	case mojom_types.SimpleType_Bool:
		return "bool"
	case mojom_types.SimpleType_Float:
		return "float32"
	case mojom_types.SimpleType_Double:
		return "float64"
	case mojom_types.SimpleType_Int8:
		return "int8"
	case mojom_types.SimpleType_Int16:
		return "int16"
	case mojom_types.SimpleType_Int32:
		return "int32"
	case mojom_types.SimpleType_Int64:
		return "int64"
	case mojom_types.SimpleType_Uint8:
		return "uint8"
	case mojom_types.SimpleType_Uint16:
		return "uint16"
	case mojom_types.SimpleType_Uint32:
		return "uint32"
	case mojom_types.SimpleType_Uint64:
		return "uint64"
	}
	panic("Non-handled mojom SimpleType. This should never happen.")
}

func (t *translator) translateStringType(mojomType mojom_types.StringType) (goType string) {
	if mojomType.Nullable {
		return "*string"
	}
	return "string"
}

func (t *translator) translateHandleType(mojomType mojom_types.HandleType) (goType string) {
	t.imports["system"] = "mojo/public/go/system"
	switch mojomType.Kind {
	default:
		panic("Unknown handle type. This should never happen.")
	case mojom_types.HandleType_Kind_Unspecified:
		goType = "system.Handle"
	case mojom_types.HandleType_Kind_MessagePipe:
		goType = "system.MessagePipeHandle"
	case mojom_types.HandleType_Kind_DataPipeConsumer:
		goType = "system.ConsumerHandle"
	case mojom_types.HandleType_Kind_DataPipeProducer:
		goType = "system.ProducerHandle"
	case mojom_types.HandleType_Kind_SharedBuffer:
		goType = "system.SharedBufferHandle"
	}
	if mojomType.Nullable {
		goType = "*" + goType
	}
	return
}

func (t *translator) translateArrayType(mojomType mojom_types.ArrayType) (goType string) {
	if mojomType.FixedLength < 0 {
		goType = "[]"
	} else {
		goType = fmt.Sprintf("[%v]", mojomType.FixedLength)
	}

	goType += t.translateType(mojomType.ElementType)

	if mojomType.Nullable {
		goType = "*" + goType
	}

	return
}

func (t *translator) translateMapType(mojomType mojom_types.MapType) (goType string) {
	goType = fmt.Sprintf("map[%s]%s",
		t.translateType(mojomType.KeyType), t.translateType(mojomType.ValueType))

	if mojomType.Nullable {
		goType = "*" + goType
	}

	return
}

func (t *translator) translateTypeReference(typeRef mojom_types.TypeReference) (goType string) {
	typeKey := *typeRef.TypeKey
	userDefinedType := t.fileGraph.ResolvedTypes[typeKey]

	typeName := t.goTypeName(*typeRef.TypeKey)

	if _, ok := userDefinedType.(*mojom_types.UserDefinedTypeInterfaceType); ok {
		if typeRef.IsInterfaceRequest {
			typeName = fmt.Sprintf("%s_Request", typeName)
		} else {
			typeName = fmt.Sprintf("%s_Pointer", typeName)
		}
	}

	srcFileInfo := userDefinedTypeDeclData(userDefinedType).SourceFileInfo
	if srcFileInfo != nil && srcFileInfo.FileName != t.currentFileName {
		pkgName := fileNameToPackageName(srcFileInfo.FileName)
		t.importMojomFile(srcFileInfo.FileName)
		typeName = fmt.Sprintf("%s.%s", pkgName, typeName)
	}

	if _, ok := userDefinedType.(*mojom_types.UserDefinedTypeUnionType); !ok && typeRef.Nullable {
		typeName = "*" + typeName
	}

	return typeName
}
