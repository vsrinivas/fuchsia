// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"bytes"
	"fmt"
	"log"
	"regexp"
	"sort"
	"strings"
	"text/template"
)

type Printer struct {
	m            *Measurer
	hIncludePath string
	buf          *bytes.Buffer
	level        int
	toPrint      []*MeasuringTape
	done         map[*MeasuringTape]bool
}

func NewPrinter(m *Measurer, mt *MeasuringTape, hIncludePath string, buf *bytes.Buffer) *Printer {
	return &Printer{
		m:            m,
		hIncludePath: hIncludePath,
		buf:          buf,
		level:        1,
		toPrint:      []*MeasuringTape{mt},
		done:         make(map[*MeasuringTape]bool),
	}
}

type tmplParams struct {
	HeaderTag              string
	HIncludePath           string
	Namespaces             []string
	LibraryNameWithSlashes string
	TargetType             string
	CcIncludes             []string
}

func (params tmplParams) RevNamespaces() []string {
	rev := make([]string, len(params.Namespaces), len(params.Namespaces))
	for i, j := 0, len(params.Namespaces)-1; i < len(params.Namespaces); i, j = i+1, j-1 {
		rev[i] = params.Namespaces[j]
	}
	return rev
}

var header = template.Must(template.New("tmpls").Parse(
	`// File is automatically generated; do not modify.
// See tools/fidl/measure-tape/README.md

#ifndef {{ .HeaderTag }}
#define {{ .HeaderTag }}

#include <{{ .LibraryNameWithSlashes }}/cpp/fidl.h>

{{ range .Namespaces }}
namespace {{ . }} {
{{- end}}

struct Size {
  explicit Size(int64_t num_bytes, int64_t num_handles)
    : num_bytes(num_bytes), num_handles(num_handles) {}

  const int64_t num_bytes;
  const int64_t num_handles;
};

// Helper function to measure {{ .TargetType }}.
//
// In most cases, the size returned is a precise size. Otherwise, the size
// returned is a safe upper-bound.
Size Measure(const {{ .TargetType }}& value);

{{ range .RevNamespaces }}
}  // {{ . }}
{{- end}}

#endif  // {{ .HeaderTag }}
`))

var ccTop = template.Must(template.New("tmpls").Parse(
	`// File is automatically generated; do not modify.
// See tools/fidl/measure-tape/README.md

#include <{{ .HIncludePath }}>
{{ range .CcIncludes }}
{{ . }}
{{- end }}
#include <zircon/types.h>

{{ range .Namespaces }}
namespace {{ . }} {
{{- end}}

namespace {

class MeasuringTape {
 public:
  MeasuringTape() = default;
`))

var ccBottom = template.Must(template.New("tmpls").Parse(`
  Size Done() {
    if (maxed_out_) {
      return Size(ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES);
    }
    return Size(num_bytes_, num_handles_);
  }

private:
  void MaxOut() { maxed_out_ = true; }

  bool maxed_out_ = false;
  int64_t num_bytes_ = 0;
  int64_t num_handles_ = 0;
};

}  // namespace

Size Measure(const {{ .TargetType }}& value) {
  MeasuringTape tape;
  tape.Measure(value);
  return tape.Done();
}

{{ range .RevNamespaces }}
}  // {{ . }}
{{- end}}
`))

var pathSeparators = regexp.MustCompile("[/_.-]")

func (p *Printer) newTmplParams() tmplParams {
	if len(p.toPrint) != 1 {
		panic("bug: should only invoke this before generation")
	}

	targetMt := p.toPrint[0]

	namespaces := []string{"measure_tape"}
	namespaces = append(namespaces, targetMt.name.LibraryName().Parts()...)

	headerTagParts := pathSeparators.Split(p.hIncludePath, -1)
	for i, part := range headerTagParts {
		headerTagParts[i] = strings.ToUpper(part)
	}
	headerTagParts = append(headerTagParts, "")
	headerTag := strings.Join(headerTagParts, "_")

	return tmplParams{
		HeaderTag:              headerTag,
		HIncludePath:           p.hIncludePath,
		LibraryNameWithSlashes: strings.Join(targetMt.name.LibraryName().Parts(), "/"),
		TargetType:             p.fmtType(targetMt.name),
		Namespaces:             namespaces,
	}
}

func (p *Printer) WriteH() {
	if err := header.Execute(p.buf, p.newTmplParams()); err != nil {
		panic(err.Error())
	}
}

func (p *Printer) WriteCc() {
	params := p.newTmplParams()
	for libraryName := range p.m.roots {
		params.CcIncludes = append(params.CcIncludes,
			fmt.Sprintf("#include <%s/cpp/fidl.h>", strings.Join(libraryName.Parts(), "/")))
	}
	sort.Strings(params.CcIncludes)

	if err := ccTop.Execute(p.buf, params); err != nil {
		panic(err.Error())
	}

	allMethods := make(map[methodID]*method)
	for len(p.toPrint) != 0 {
		mt, remaining := p.toPrint[0], p.toPrint[1:]
		p.toPrint = remaining
		if p.done[mt] {
			continue
		}
		p.done[mt] = true

		expr := exprLocal("value", mt.kind, false)
		for _, m := range []*method{
			mt.newMeasureMethod(expr),
			mt.newMeasureOutOfLineMethod(p, expr),
			mt.newMeasureHandlesMethod(expr),
		} {
			allMethods[m.id] = m
		}
	}

	pruneEmptyMethods(allMethods)

	methodsToPrint := make([]methodID, 0, len(allMethods))
	for id := range allMethods {
		methodsToPrint = append(methodsToPrint, id)
	}
	sort.Sort(byTargetTypeThenKind(methodsToPrint))
	for _, id := range methodsToPrint {
		p.buf.WriteString("\n")
		allMethods[id].print(p)
	}

	if err := ccBottom.Execute(p.buf, params); err != nil {
		panic(err.Error())
	}
}

const indent = "  "

func (p *Printer) writef(format string, a ...interface{}) {
	for i := 0; i < p.level; i++ {
		p.buf.WriteString(indent)
	}
	p.buf.WriteString(fmt.Sprintf(format, a...))
}

func (p *Printer) indent(fn func()) {
	p.level++
	fn()
	p.level--
}

func (p *Printer) add(mt *MeasuringTape) {
	if !p.done[mt] {
		p.toPrint = append(p.toPrint, mt)
	}
}

func (mt *MeasuringTape) assertOnlyStructUnionTable() {
	switch mt.kind {
	case kUnion:
		return
	case kStruct:
		return
	case kTable:
		return
	default:
		log.Panicf("should not be reachable for kind %v", mt.kind)
	}
}

func (mt *MeasuringTape) newMeasureMethod(expr Expression) *method {
	mt.assertOnlyStructUnionTable()

	var body block
	switch mt.kind {
	case kUnion:
		body.emitAddNumBytes(exprNum(mt.inlineNumBytes))
	case kStruct:
		body.emitAddNumBytes(valFidlAlign(exprNum(mt.inlineNumBytes)))
		if mt.hasHandles {
			body.emitInvoke(mt.methodIDOf(measureHandles), expr)
		}
	case kTable:
		body.emitAddNumBytes(exprNum(mt.inlineNumBytes))
	}
	body.emitInvoke(mt.methodIDOf(measureOutOfLine), expr)
	return newMethod(mt.methodIDOf(measure), expr, &body)
}

func (mt *MeasuringTape) newMeasureOutOfLineMethod(p *Printer, expr Expression) *method {
	mt.assertOnlyStructUnionTable()

	var body block
	switch mt.kind {
	case kUnion:
		mt.writeUnionOutOfLine(p, expr, &body)
	case kStruct:
		mt.writeStructOutOfLine(p, expr, &body)
	case kTable:
		mt.writeTableOutOfLine(p, expr, &body)
	}
	return newMethod(mt.methodIDOf(measureOutOfLine), expr, &body)
}

func (mt *MeasuringTape) newMeasureHandlesMethod(expr Expression) *method {
	mt.assertOnlyStructUnionTable()

	var body block
	if mt.hasHandles {
		switch mt.kind {
		case kStruct:
			for _, member := range mt.members {
				if member.mt.kind == kHandle {
					// TODO(fxb/49488): Conditionally increase for nullable handles.
					body.emitAddNumHandles(exprNum(1))
				} else if member.mt.hasHandles {
					body.emitInvoke(
						member.mt.methodIDOf(measureHandles),
						valMemberOf(expr, member.name, member.mt.kind, member.mt.nullable))
				}
			}
		}
	}
	return newMethod(mt.methodIDOf(measureHandles), expr, &body)
}

type invokeKind int

const (
	_ invokeKind = iota
	inlineAndOutOfLine
	outOfLineOnly
)

func (member *measuringTapeMember) guardNullableAccess(expr Expression, body *block, fn func(*block)) {
	if member.mt.nullable {
		var guardBody block
		body.emitGuard(expr, &guardBody)
		fn(&guardBody)
	} else {
		fn(body)
	}
}

func (member *measuringTapeMember) writeInvoke(expr Expression, p *Printer, body *block, mode invokeKind) {
	switch member.mt.kind {
	case kString:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(16))
		}
		member.guardNullableAccess(expr, body, func(guardBody *block) {
			guardBody.emitAddNumBytes(valFidlAlign(exprLength(expr)))
		})
	case kVector:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(16))
		}
		member.guardNullableAccess(expr, body, func(guardBody *block) {
			if mode == inlineAndOutOfLine {
				if member.mt.elementMt.kind == kHandle ||
					(!member.mt.elementMt.hasOutOfLine && member.mt.elementMt.hasHandles) {
					// TODO(fxb/49488): Conditionally increase for nullable handles.
					guardBody.emitAddNumHandles(exprMult(
						exprLength(expr),
						exprNum(member.mt.elementMt.inlineNumHandles)))
				}
			}
			if member.mt.elementMt.hasOutOfLine {
				memberMt := measuringTapeMember{
					name: fmt.Sprintf("%s_elem", member.name),
					mt:   member.mt.elementMt,
				}
				var iterateBody block
				local := exprLocal(memberMt.name, memberMt.mt.kind, memberMt.mt.nullable)
				guardBody.emitIterate(
					local, expr,
					&iterateBody)
				memberMt.writeInvoke(
					local,
					p, &iterateBody, inlineAndOutOfLine)
			} else {
				guardBody.emitAddNumBytes(
					valFidlAlign(
						exprMult(
							exprLength(expr),
							exprNum(member.mt.elementMt.inlineNumBytes))))
			}
		})
	case kArray:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(valFidlAlign(exprNum(member.mt.inlineNumBytes)))
			if member.mt.elementMt.kind == kHandle || (!member.mt.hasOutOfLine && member.mt.hasHandles) {
				// TODO(fxb/49488): Conditionally increase for nullable handles.
				body.emitAddNumHandles(exprNum(member.mt.inlineNumHandles))
			}
		}
		if member.mt.hasOutOfLine {
			memberMt := measuringTapeMember{
				name: fmt.Sprintf("%s_elem", member.name),
				mt:   member.mt.elementMt,
			}
			var iterateBody block
			local := exprLocal(memberMt.name, memberMt.mt.kind, memberMt.mt.nullable)
			body.emitIterate(
				local, expr,
				&iterateBody)
			memberMt.writeInvoke(
				local,
				p, &iterateBody, outOfLineOnly)
		}
	case kPrimitive:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(8))
		}
	case kHandle:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(8))
			// TODO(fxb/49488): Conditionally increase for nullable handles.
			body.emitAddNumHandles(exprNum(1))
		}
	default:
		p.add(member.mt)
		member.guardNullableAccess(expr, body, func(guardBody *block) {
			switch mode {
			case inlineAndOutOfLine:
				guardBody.emitInvoke(member.mt.methodIDOf(measure), expr)
			case outOfLineOnly:
				guardBody.emitInvoke(member.mt.methodIDOf(measureOutOfLine), expr)
			}
		})
	}
}

func (mt *MeasuringTape) writeStructOutOfLine(p *Printer, expr Expression, body *block) {
	for _, member := range mt.members {
		member.writeInvoke(
			valMemberOf(expr, member.name, member.mt.kind, member.mt.nullable),
			p, body, outOfLineOnly)
	}
}

func (mt *MeasuringTape) writeUnionOutOfLine(p *Printer, expr Expression, body *block) {
	variants := make(map[string]*block)

	// known
	for _, member := range mt.members {
		var variantBody block
		variants[member.name] = &variantBody
		member.writeInvoke(
			valMemberOf(expr, member.name, member.mt.kind, member.mt.nullable),
			p, &variantBody, inlineAndOutOfLine)
	}

	// unknown
	{
		var variantBody block
		variantBody.emitMaxOut()
		variants[unknownVariant] = &variantBody
	}

	body.emitSelectVariant(expr, mt.name, variants)
}

func (mt *MeasuringTape) writeTableOutOfLine(p *Printer, expr Expression, body *block) {
	maxOrdinalLocal := body.emitDeclareMaxOrdinal()
	for _, member := range mt.members {
		var guardBody block
		body.emitGuard(
			valHasMember(expr, member.name),
			&guardBody)
		member.writeInvoke(
			valMemberOf(expr, member.name, member.mt.kind, member.mt.nullable),
			p, &guardBody, inlineAndOutOfLine)
		guardBody.emitSetMaxOrdinal(maxOrdinalLocal, member.ordinal)
	}
	body.emitAddNumBytes(exprMult(exprNum(16), maxOrdinalLocal))
}
