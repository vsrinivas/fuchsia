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

	fidlcommon "fidl/compiler/backend/common"
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

		methods := []*method{
			mt.newMeasureMethod(),
			mt.newMeasureOutOfLineMethod(p),
		}
		if mt.hasHandles {
			methods = append(methods, mt.newMeasureHandlesMethod())
		}
		for _, m := range methods {
			allMethods[m.id] = m
		}
	}

	// TODO(fxb/50010): Collect and apply method pruning before printing.
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

func (mt *MeasuringTape) newMeasureMethod() *method {
	mt.assertOnlyStructUnionTable()

	var body block
	switch mt.kind {
	case kUnion:
		body.emitAddNumBytes("sizeof(fidl_xunion_t)")
	case kStruct:
		body.emitAddNumBytes("FIDL_ALIGN(%d)", mt.inlineNumBytes)
		if mt.hasHandles {
			body.emitInvoke(mt.methodIDOf(measureHandles), "value")
		}
	case kTable:
		body.emitAddNumBytes("sizeof(fidl_table_t)")
	}
	body.emitInvoke(mt.methodIDOf(measureOutOfLine), "value")
	return newMethod(mt.methodIDOf(measure), &body)
}

func (mt *MeasuringTape) newMeasureOutOfLineMethod(p *Printer) *method {
	mt.assertOnlyStructUnionTable()

	var body block
	switch mt.kind {
	case kUnion:
		mt.writeUnionOutOfLine(p, &body)
	case kStruct:
		mt.writeStructOutOfLine(p, &body)
	case kTable:
		mt.writeTableOutOfLine(p, &body)
	}
	return newMethod(mt.methodIDOf(measureOutOfLine), &body)
}

func (mt *MeasuringTape) newMeasureHandlesMethod() *method {
	mt.assertOnlyStructUnionTable()

	var body block
	if mt.hasHandles {
		switch mt.kind {
		case kStruct:
			for _, member := range mt.members {
				if member.mt.kind == kHandle {
					// TODO(fxb/49488): Conditionally increase for nullable handles.
					body.emitAddNumHandles("1")
				} else if member.mt.hasHandles {
					body.emitInvoke(
						member.mt.methodIDOf(measureHandles),
						"value.%s", fidlcommon.ToSnakeCase(member.name))
				}
			}
		}
	}
	return newMethod(mt.methodIDOf(measureHandles), &body)
}

type invokeKind int

const (
	_ invokeKind = iota
	inlineAndOutOfLine
	outOfLineOnly
)

type fieldKind int

const (
	_ fieldKind = iota
	directAccessField
	throughAccessorField
	localVar
)

type derefOp int

const (
	_ derefOp = iota
	directDeref
	pointerDeref
)

func (op derefOp) String() string {
	switch op {
	case directDeref:
		return "."
	case pointerDeref:
		return "->"
	default:
		log.Panicf("should not be reachable for op %v", op)
		return ""
	}
}

func (member *measuringTapeMember) guardNullableAccess(body *block, fieldMode fieldKind, fn func(*block, derefOp)) {
	if member.mt.nullable {
		var guardBody block
		body.emitGuard(member.accessor(fieldMode), &guardBody)
		fn(&guardBody, pointerDeref)
	} else {
		fn(body, directDeref)
	}
}

func (member *measuringTapeMember) accessor(fieldMode fieldKind) string {
	switch fieldMode {
	case directAccessField:
		return fmt.Sprintf("value.%s", fidlcommon.ToSnakeCase(member.name))
	case throughAccessorField:
		return fmt.Sprintf("value.%s()", fidlcommon.ToSnakeCase(member.name))
	case localVar:
		return member.name
	default:
		log.Panic("incorrect fieldKind")
		return ""
	}
}

func (member *measuringTapeMember) writeInvoke(p *Printer, body *block, mode invokeKind, fieldMode fieldKind) {
	switch member.mt.kind {
	case kString:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes("sizeof(fidl_string_t)")
		}
		member.guardNullableAccess(body, fieldMode, func(guardBody *block, op derefOp) {
			guardBody.emitAddNumBytes("FIDL_ALIGN(%s%slength())", member.accessor(fieldMode), op)
		})
	case kVector:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes("sizeof(fidl_vector_t)")
		}
		member.guardNullableAccess(body, fieldMode, func(guardBody *block, op derefOp) {
			if mode == inlineAndOutOfLine {
				if member.mt.elementMt.kind == kHandle ||
					(!member.mt.elementMt.hasOutOfLine && member.mt.elementMt.hasHandles) {
					// TODO(fxb/49488): Conditionally increase for nullable handles.
					guardBody.emitAddNumHandles("%s%ssize() * %d",
						member.accessor(fieldMode), op, member.mt.elementMt.inlineNumHandles)
				}
			}
			if member.mt.elementMt.hasOutOfLine {
				memberMt := measuringTapeMember{
					name: fmt.Sprintf("%s_elem", member.name),
					mt:   member.mt.elementMt,
				}
				var deref string
				if op == pointerDeref {
					deref = "*"
				}
				var iterateBody block
				guardBody.emitIterate(
					memberMt.name,
					fmt.Sprintf("%s%s", deref, member.accessor(fieldMode)),
					&iterateBody)
				memberMt.writeInvoke(p, &iterateBody, inlineAndOutOfLine, localVar)
			} else {
				guardBody.emitAddNumBytes("FIDL_ALIGN(%s%ssize() * %d)",
					member.accessor(fieldMode), op, member.mt.elementMt.inlineNumBytes)
			}
		})
	case kArray:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes("FIDL_ALIGN(%d)", member.mt.inlineNumBytes)
			if member.mt.elementMt.kind == kHandle || (!member.mt.hasOutOfLine && member.mt.hasHandles) {
				// TODO(fxb/49488): Conditionally increase for nullable handles.
				body.emitAddNumHandles("%d", member.mt.inlineNumHandles)
			}
		}
		if member.mt.hasOutOfLine {
			memberMt := measuringTapeMember{
				name: fmt.Sprintf("%s_elem", member.name),
				mt:   member.mt.elementMt,
			}
			var iterateBody block
			body.emitIterate(memberMt.name, member.accessor(fieldMode), &iterateBody)
			memberMt.writeInvoke(p, &iterateBody, outOfLineOnly, localVar)
		}
	case kPrimitive:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes("8")
		}
	case kHandle:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes("8")
			// TODO(fxb/49488): Conditionally increase for nullable handles.
			body.emitAddNumHandles("1")
		}
	default:
		p.add(member.mt)
		member.guardNullableAccess(body, fieldMode, func(guardBody *block, _ derefOp) {
			switch mode {
			case inlineAndOutOfLine:
				guardBody.emitInvoke(member.mt.methodIDOf(measure), member.accessor(fieldMode))
			case outOfLineOnly:
				guardBody.emitInvoke(member.mt.methodIDOf(measureOutOfLine), member.accessor(fieldMode))
			}
		})
	}
}

func (mt *MeasuringTape) writeStructOutOfLine(p *Printer, body *block) {
	for _, member := range mt.members {
		member.writeInvoke(p, body, outOfLineOnly, directAccessField)
	}
}

func (mt *MeasuringTape) writeUnionOutOfLine(p *Printer, body *block) {
	variants := make(map[string]*block)

	// known
	for _, member := range mt.members {
		var variantBody block
		variants[member.name] = &variantBody
		member.writeInvoke(p, &variantBody, inlineAndOutOfLine, throughAccessorField)
	}

	// unknown
	{
		var variantBody block
		variantBody.emitMaxOut()
		variants[unknownVariant] = &variantBody
	}

	body.emitSelectVariant("value", mt.name, variants)
}

func (mt *MeasuringTape) writeTableOutOfLine(p *Printer, body *block) {
	body.emitDeclareMaxOrdinal()
	for _, member := range mt.members {
		var guardBody block
		body.emitGuard(fmt.Sprintf("value.has_%s()", fidlcommon.ToSnakeCase(member.name)), &guardBody)
		member.writeInvoke(p, &guardBody, inlineAndOutOfLine, throughAccessorField)
		guardBody.emitSetMaxOrdinal(member.ordinal)
	}
	body.emitAddNumBytes("sizeof(fidl_envelope_t) * max_ordinal")
}
