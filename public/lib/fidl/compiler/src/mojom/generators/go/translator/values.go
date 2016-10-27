// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"fmt"
	"log"

	"mojom/generated/mojom_types"
)

func (t *translator) translateValue(mojomValue mojom_types.Value) (value string) {
	switch m := mojomValue.(type) {
	default:
		log.Panicf("Not implemented yet: %s", mojomValue)
	case *mojom_types.ValueLiteralValue:
		value = t.translateLiteralValue(m.Value)
	case *mojom_types.ValueEnumValueReference:
		value = t.translateEnumValueReference(m.Value)
	case *mojom_types.ValueConstantReference:
		value = t.translateConstantReference(m.Value)
	case *mojom_types.ValueBuiltinValue:
		log.Panicln("There is no way to represent built-in mojom values as literals in go.")
	}

	return value
}

func (t *translator) translateConstantReference(constRef mojom_types.ConstantReference) (value string) {
	declaredConstant := t.fileGraph.ResolvedConstants[constRef.ConstantKey]
	srcFileInfo := declaredConstant.DeclData.SourceFileInfo
	value = t.goConstName(constRef.ConstantKey)

	if srcFileInfo != nil && srcFileInfo.FileName != t.currentFileName {
		pkgName := fileNameToPackageName(srcFileInfo.FileName)
		t.importMojomFile(srcFileInfo.FileName)
		value = fmt.Sprintf("%s.%s", pkgName, value)
	}

	return
}

func (t *translator) translateLiteralValue(literalValue mojom_types.LiteralValue) (value string) {
	if s, ok := literalValue.(*mojom_types.LiteralValueStringValue); ok {
		return fmt.Sprintf("%q", s.Value)
	}
	return fmt.Sprintf("%v", literalValue.Interface())
}

func (t *translator) translateEnumValueReference(enumValueRef mojom_types.EnumValueReference) string {
	enumTypeRef := mojom_types.TypeReference{TypeKey: &enumValueRef.EnumTypeKey}
	enumName := t.translateTypeReference(enumTypeRef)

	e := t.GetUserDefinedType(enumValueRef.EnumTypeKey)
	enum, ok := e.Interface().(mojom_types.MojomEnum)
	if !ok {
		log.Panicf("%s is not an enum.\n", userDefinedTypeShortName(e))
	}

	enumValueName := formatName(*enum.Values[enumValueRef.EnumValueIndex].DeclData.ShortName)
	return fmt.Sprintf("%s_%s", enumName, enumValueName)
}

func (t *translator) resolveConstRef(value mojom_types.Value) mojom_types.Value {
	constRef, ok := value.(*mojom_types.ValueConstantReference)
	if !ok {
		return value
	}

	declaredConstant := t.fileGraph.ResolvedConstants[constRef.Value.ConstantKey]
	return t.resolveConstRef(declaredConstant.Value)
}
