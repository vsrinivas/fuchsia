// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"fmt"
	"log"

	"fidl/compiler/generated/fidl_types"
)

func (t *translator) translateValue(mojomValue fidl_types.Value) (value string) {
	switch m := mojomValue.(type) {
	default:
		log.Panicf("Not implemented yet: %s", mojomValue)
	case *fidl_types.ValueLiteralValue:
		value = t.translateLiteralValue(m.Value)
	case *fidl_types.ValueEnumValueReference:
		value = t.translateEnumValueReference(m.Value)
	case *fidl_types.ValueConstantReference:
		value = t.translateConstantReference(m.Value)
	case *fidl_types.ValueBuiltinValue:
		log.Panicln("There is no way to represent built-in mojom values as literals in go.")
	}

	return value
}

func (t *translator) translateConstantReference(constRef fidl_types.ConstantReference) (value string) {
	declaredConstant := t.fileGraph.ResolvedConstants[constRef.ConstantKey]
	srcFileInfo := declaredConstant.DeclData.SourceFileInfo
	value = t.goConstName(constRef.ConstantKey)

	if srcFileInfo != nil && srcFileInfo.FileName != t.currentFileName {
		pkgName := fileNameToPackageName(srcFileInfo.FileName)
		t.importFidlFile(srcFileInfo.FileName)
		value = fmt.Sprintf("%s.%s", pkgName, value)
	}

	return
}

func (t *translator) translateLiteralValue(literalValue fidl_types.LiteralValue) (value string) {
	if s, ok := literalValue.(*fidl_types.LiteralValueStringValue); ok {
		return fmt.Sprintf("%q", s.Value)
	}
	return fmt.Sprintf("%v", literalValue.Interface())
}

func (t *translator) translateEnumValueReference(enumValueRef fidl_types.EnumValueReference) string {
	enumTypeRef := fidl_types.TypeReference{TypeKey: &enumValueRef.EnumTypeKey}
	enumName := t.translateTypeReference(enumTypeRef)

	e := t.GetUserDefinedType(enumValueRef.EnumTypeKey)
	enum, ok := e.Interface().(fidl_types.FidlEnum)
	if !ok {
		log.Panicf("%s is not an enum.\n", userDefinedTypeShortName(e))
	}

	enumValueName := formatName(*enum.Values[enumValueRef.EnumValueIndex].DeclData.ShortName)
	return fmt.Sprintf("%s_%s", enumName, enumValueName)
}

func (t *translator) resolveConstRef(value fidl_types.Value) fidl_types.Value {
	constRef, ok := value.(*fidl_types.ValueConstantReference)
	if !ok {
		return value
	}

	declaredConstant := t.fileGraph.ResolvedConstants[constRef.Value.ConstantKey]
	return t.resolveConstRef(declaredConstant.Value)
}
