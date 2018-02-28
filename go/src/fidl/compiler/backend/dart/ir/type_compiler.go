// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/types"
	"fmt"
	"log"
	"strconv"
	"strings"
)

func formatIndent(indent int) string {
	return strings.Repeat("  ", indent)
}

func formatBool(val bool) string {
	return strconv.FormatBool(val)
}

func formatConstant(val *types.Constant) string {
	if val == nil {
		return "null"
	}

	if val.Kind == types.LiteralConstant {
		literal := val.Literal
		if literal.Kind == types.NumericLiteral {
			return literal.Value
		}
	}

	// TODO(TO-746): Either support other kinds of constants or simplify this code
	// when the frontend does the resolution.
	return "0"
}

var primitiveEncodedSize = map[types.PrimitiveSubtype]int{
	types.Bool:    1,
	types.Status:  4,
	types.Int8:    1,
	types.Int16:   2,
	types.Int32:   4,
	types.Int64:   8,
	types.Uint8:   1,
	types.Uint16:  2,
	types.Uint32:  4,
	types.Uint64:  8,
	types.Float32: 4,
	types.Float64: 8,
}

func formatEncodedSizeForPrimitiveSubtype(t types.PrimitiveSubtype) string {
	return strconv.Itoa(primitiveEncodedSize[t])
}

type TypeCompiler struct {
	encodedSizeMap map[types.Identifier]string
}

func (c *TypeCompiler) encodedSize(t types.Type) string {
	switch t.Kind {
	case types.ArrayType:
		return fmt.Sprintf("%s * %s", formatConstant(t.ElementCount), c.encodedSize(*t.ElementType))
	case types.VectorType:
		return "16"
	case types.StringType:
		return "16"
	case types.HandleType:
		return "8"
	case types.RequestType:
		return "8"
	case types.PrimitiveType:
		return formatEncodedSizeForPrimitiveSubtype(t.PrimitiveSubtype)
	case types.IdentifierType:
		// TODO(abarth): We'll probably need the encodedSizeMap to use
		// CompoundIdentifiers as keys once we support multiple libraries.
		return c.encodedSizeMap[t.Identifier[0]]
	default:
		log.Fatal("Unknown type kind:", t.Kind)
	}
	return ""
}

func (c *TypeCompiler) arrayTypeExpr(t types.Type, indent int) string {
	indentStr := formatIndent(indent)
	elementStr := fmt.Sprintf("%s  element: %s,\n", indentStr, c.expr(*t.ElementType, indent+2))
	elementCountStr := fmt.Sprintf("%s  elementCount: %s,\n", indentStr, formatConstant(t.ElementCount))
	elementSizeStr := fmt.Sprintf("%s  elementSize: %s,\n", indentStr, c.encodedSize(*t.ElementType))
	return fmt.Sprintf("const $fidl.ArrayType(\n%s%s%s%s)", elementStr, elementCountStr, elementSizeStr, indentStr)
}

func (c *TypeCompiler) vectorTypeExpr(t types.Type, indent int) string {
	indentStr := formatIndent(indent)
	elementStr := fmt.Sprintf("%s  element: %s,\n", indentStr, c.expr(*t.ElementType, indent+2))
	maybeElementCountStr := fmt.Sprintf("%s  count: %s,\n", indentStr, formatConstant(t.ElementCount))
	elementSizeStr := fmt.Sprintf("%s  elementSize: %s,\n", indentStr, c.encodedSize(*t.ElementType))
	nullableStr := fmt.Sprintf("%s  nullable: %s,", indentStr, formatBool(t.Nullable))
	return fmt.Sprintf("const $fidl.VectorType(\n%s%s%s%s%s)",
		elementStr, maybeElementCountStr, elementSizeStr, nullableStr, indentStr)
}

func (c *TypeCompiler) stringTypeExpr(t types.Type, indent int) string {
	return fmt.Sprintf("const $fidl.StringType(maybeElementCount: %s, nullable: %s)",
		formatConstant(t.ElementCount), formatBool(t.Nullable))
}

func (c *TypeCompiler) handleTypeExpr(t types.Type, indent int) string {
	return fmt.Sprintf("const $fidl.HandleType(nullable: %s)",
		formatBool(t.Nullable))
}

func (c *TypeCompiler) primitiveTypeExpr(t types.Type, indent int) string {
	return "const $fidl.PrimitiveType()"
}

func (c *TypeCompiler) identifierTypeExpr(t types.Type, indent int) string {
	return c.CompountSymbol(t.Identifier)
}

func (c *TypeCompiler) expr(t types.Type, indent int) string {
	switch t.Kind {
	case types.ArrayType:
		return c.arrayTypeExpr(t, indent)
	case types.VectorType:
		return c.vectorTypeExpr(t, indent)
	case types.StringType:
		return c.stringTypeExpr(t, indent)
	case types.HandleType:
		return c.handleTypeExpr(t, indent)
	case types.RequestType:
		return c.handleTypeExpr(t, indent)
	case types.PrimitiveType:
		return c.primitiveTypeExpr(t, indent)
	case types.IdentifierType:
		return c.identifierTypeExpr(t, indent)
	default:
		log.Fatal("Unknown type kind:", t.Kind)
	}
	return ""
}

func (c *TypeCompiler) structMemberTypeExpr(m types.StructMember, indent int) string {
	indentStr := formatIndent(indent)
	typeStr := fmt.Sprintf("%s  type: %s,\n", indentStr, c.expr(m.Type, indent+2))
	offsetStr := fmt.Sprintf("%s  offset: %v,\n", indentStr, m.Offset)
	return fmt.Sprintf("const $fidl.MemberType(\n%s%s%s)", typeStr, offsetStr, indentStr)
}

func (c *TypeCompiler) unionMemberTypeExpr(m types.UnionMember, indent int) string {
	indentStr := formatIndent(indent)
	typeStr := fmt.Sprintf("%s  type: %s,\n", indentStr, c.expr(m.Type, indent+2))
	offsetStr := fmt.Sprintf("%s  offset: %v,\n", indentStr, m.Offset)
	return fmt.Sprintf("const $fidl.MemberType(\n%s%s%s)", typeStr, offsetStr, indentStr)
}

func (c *TypeCompiler) parameterTypeExpr(m types.Parameter, indent int) string {
	indentStr := formatIndent(indent)
	typeStr := fmt.Sprintf("%s  type: %s,\n", indentStr, c.expr(m.Type, indent+2))
	offsetStr := fmt.Sprintf("%s  offset: %v,\n", indentStr, m.Offset)
	return fmt.Sprintf("const $fidl.MemberType(\n%s%s%s)", typeStr, offsetStr, indentStr)
}

func NewTypeCompiler(r types.Root) *TypeCompiler {
	c := TypeCompiler{}
	c.encodedSizeMap = map[types.Identifier]string{}

	for _, v := range r.Enums {
		c.encodedSizeMap[v.Name] = formatEncodedSizeForPrimitiveSubtype(v.Type)
	}

	for _, v := range r.Interfaces {
		c.encodedSizeMap[v.Name] = "8"
	}

	for _, v := range r.Structs {
		c.encodedSizeMap[v.Name] = strconv.Itoa(v.Size)
	}

	for _, v := range r.Unions {
		c.encodedSizeMap[v.Name] = strconv.Itoa(v.Size)
	}

	return &c
}

func (c *TypeCompiler) CompountSymbol(ident types.CompoundIdentifier) string {
	// TODO(abarth): Figure out how to reference type symbols in other libraries.
	return fmt.Sprintf("k%sType", ident[0])
}

func (c *TypeCompiler) Symbol(ident types.Identifier) string {
	return fmt.Sprintf("k%sType", ident)
}

func (c *TypeCompiler) Expr(t types.Type) string {
	return c.expr(t, 0)
}

func (c *TypeCompiler) StructExpr(t types.Struct) string {
	members := []string{}

	for _, v := range t.Members {
		members = append(members, c.structMemberTypeExpr(v, 3))
	}

	membersStr := strings.Join(members, ",\n    ")

	return fmt.Sprintf("const $fidl.StructType(\n  members: const <$fidl.MemberType>[\n    %s\n  ]\n)", membersStr)
}

func (c *TypeCompiler) UnionExpr(t types.Union) string {
	members := []string{}

	for _, v := range t.Members {
		members = append(members, c.unionMemberTypeExpr(v, 2))
	}

	membersStr := strings.Join(members, ",\n    ")

	return fmt.Sprintf("const $fidl.UnionType(\n  members: const <$fidl.MemberType>[\n    %s\n  ]\n)", membersStr)
}

func (c *TypeCompiler) MethodExpr(t types.Method) string {
	requestStr := "null"
	responseStr := "null"

	if t.HasRequest {
		request := []string{}

		for _, v := range t.Request {
			request = append(request, c.parameterTypeExpr(v, 2))
		}

		requestStr = fmt.Sprintf("const <$fidl.MemberType>[\n    %s\n  ]", strings.Join(request, ",\n    "))
	}

	if t.HasResponse {
		response := []string{}

		for _, v := range t.Response {
			response = append(response, c.parameterTypeExpr(v, 2))
		}

		responseStr = fmt.Sprintf("const <$fidl.MemberType>[\n    %s\n  ]", strings.Join(response, ",\n    "))
	}

	return fmt.Sprintf("const $fidl.MethodType(\n  request: %s,\n  response: %s,\n)", requestStr, responseStr)
}
