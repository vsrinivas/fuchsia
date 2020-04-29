// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"fmt"
	"log"
	"sort"
	"strings"

	fidlcommon "fidl/compiler/backend/common"
)

// TODO(fxb/50011): Generalize all the code below for Rust backend.
//
// All this code is expected to move to a separate package, specialized by
// backend. While right now, this operates on unexported types and fields,
// this will be refactored to use a visitor pattern, and only exported types.

func (b *block) print(p *Printer) {
	b.forAllStatements(func(stmt *statement) {
		var args []string
		for _, arg := range stmt.args {
			args = append(args, formatExpr{arg}.String())
		}
		switch stmt.kind {
		case maxOut:
			p.writef("MaxOut();\n")
		case addNumBytes:
			p.writef("num_bytes_ += %s;\n", args[0])
		case addNumHandles:
			p.writef("num_handles_ += %s;\n", args[0])
		case invoke:
			p.writef("%s(%s);\n", fmtMethodKind(stmt.id.kind), args[0])
		case guard:
			p.writef("if (%s) {\n", args[0])
			p.indent(func() {
				stmt.body.print(p)
			})
			p.writef("}\n")
		case iterate:
			var deref string
			if stmt.args[1].Nullable() {
				deref = "*"
			}
			p.writef("for (const auto& %s : %s%s) {\n", args[0], deref, args[1])
			p.indent(func() {
				stmt.body.print(p)
			})
			p.writef("}\n")
		case selectVariant:
			p.writef("switch (%s.Which()) {\n", args[0])
			p.indent(func() {
				var variants []string
				for variant := range stmt.variants {
					variants = append(variants, variant)
				}
				sort.Strings(variants)
				for _, variant := range variants {
					if variant == unknownVariant {
						p.writef("case %s:\n", p.fmtUnknownVariant(stmt.targetType))
					} else {
						p.writef("case %s:\n", p.fmtKnownVariant(stmt.targetType, variant))
					}
					p.indent(func() {
						stmt.variants[variant].print(p)
						p.writef("break;\n")
					})
				}
			})
			p.writef("}\n")
		case declareMaxOrdinal:
			p.writef("int32_t %s = 0;\n", args[0])
		case setMaxOrdinal:
			p.writef("%s = %s;\n", args[0], args[1])
		}
	})
}

func (m *method) print(p *Printer) {
	p.writef("void %s(const %s& %s) {\n", fmtMethodKind(m.id.kind), p.fmtType(m.id.targetType), formatExpr{m.arg})
	p.indent(func() {
		m.body.print(p)
	})
	p.writef("}\n")
}

func fmtMethodKind(kind methodKind) string {
	switch kind {
	case measure:
		return "Measure"
	case measureOutOfLine:
		return "MeasureOutOfLine"
	case measureHandles:
		return "MeasureHandles"
	default:
		log.Panicf("should not be reachable for kind %v", kind)
		return ""
	}
}

func (p *Printer) fmtType(name fidlcommon.Name) string {
	return fmt.Sprintf("::%s::%s", strings.Join(name.LibraryName().Parts(), "::"), name.DeclarationName())
}

func (p *Printer) fmtKnownVariant(name fidlcommon.Name, variant string) string {
	return fmt.Sprintf("%s::Tag::k%s", p.fmtType(name), fidlcommon.ToUpperCamelCase(variant))
}

func (p *Printer) fmtUnknownVariant(name fidlcommon.Name) string {
	return fmt.Sprintf("%s::Tag::Invalid", p.fmtType(name))
}

type formatExpr struct{ Expression }

func (expr formatExpr) String() string {
	return expr.Fmt(expr)
}

var _ ExpressionFormatter = formatExpr{}

func (formatExpr) CaseNum(num int) string {
	return fmt.Sprintf("%d", num)
}

func (formatExpr) CaseLocal(name string, _ tapeKind) string {
	return name
}

func (formatExpr) CaseMemberOf(expr Expression, member string) string {
	var accessor string
	if kind := expr.AssertKind(kStruct, kUnion, kTable); kind != kStruct {
		accessor = "()"
	}
	return fmt.Sprintf("%s%s%s%s", formatExpr{expr}, getDerefOp(expr), fidlcommon.ToSnakeCase(member), accessor)
}

func (formatExpr) CaseFidlAlign(expr Expression) string {
	return fmt.Sprintf("FIDL_ALIGN(%s)", formatExpr{expr})
}

func (formatExpr) CaseLength(expr Expression) string {
	var op string
	switch expr.AssertKind(kString, kVector) {
	case kString:
		op = "length"
	case kVector:
		op = "size"
	}
	return fmt.Sprintf("%s%s%s()", formatExpr{expr}, getDerefOp(expr), op)
}

func (formatExpr) CaseHasMember(expr Expression, member string) string {
	return fmt.Sprintf("%s%shas_%s()", formatExpr{expr}, getDerefOp(expr), fidlcommon.ToSnakeCase(member))
}

func (formatExpr) CaseMult(lhs, rhs Expression) string {
	return fmt.Sprintf("%s * %s", formatExpr{lhs}, formatExpr{rhs})
}

func getDerefOp(expr Expression) string {
	if expr.Nullable() {
		return "->"
	}
	return "."
}
