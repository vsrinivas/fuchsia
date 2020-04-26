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
	for _, stmt := range b.stmts {
		switch stmt.kind {
		case maxOut:
			p.writef("MaxOut();\n")
		case addNumBytes:
			p.writef("num_bytes_ += %s;\n", stmt.args[0])
		case addNumHandles:
			p.writef("num_handles_ += %s;\n", stmt.args[0])
		case invoke:
			p.writef("%s(%s);\n", fmtMethodKind(stmt.id.kind), stmt.args[0])
		case guard:
			p.writef("if (%s) {\n", stmt.args[0])
			p.indent(func() {
				stmt.body.print(p)
			})
			p.writef("}\n")
		case iterate:
			p.writef("for (const auto& %s : %s) {\n", stmt.args[0], stmt.args[1])
			p.indent(func() {
				stmt.body.print(p)
			})
			p.writef("}\n")
		case selectVariant:
			p.writef("switch (%s.Which()) {\n", stmt.args[0])
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
			p.writef("int32_t max_ordinal = 0;\n")
		case setMaxOrdinal:
			p.writef("max_ordinal = %s;\n", stmt.args[0])
		}
	}
}

func (b *method) print(p *Printer) {
	p.writef("void %s(const %s& value) {\n", fmtMethodKind(b.id.kind), p.fmtType(b.id.targetType))
	p.indent(func() {
		b.body.print(p)
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
