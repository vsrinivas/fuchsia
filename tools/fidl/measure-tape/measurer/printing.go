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
		TargetType:             targetMt.nameToType(),
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

	p.buf.WriteString("\n")
	for len(p.toPrint) != 0 {
		mt, remaining := p.toPrint[0], p.toPrint[1:]
		p.toPrint = remaining
		if p.done[mt] {
			continue
		}
		p.done[mt] = true
		mt.write(p)
		p.writef("\n")
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

func (mt *MeasuringTape) write(p *Printer) {
	p.writef("void Measure(const %s& value) {\n", mt.nameToType())
	p.indent(func() {
		switch mt.kind {
		case kUnion:
			p.writef("num_bytes_ += sizeof(fidl_xunion_t);\n")
		case kStruct:
			p.writef("num_bytes_ += FIDL_ALIGN(%d);\n", mt.inlineNumBytes)
			if mt.hasHandles {
				p.writef("MeasureHandles(value);\n")
			}
		case kTable:
			p.writef("num_bytes_ += sizeof(fidl_table_t);\n")
		default:
			log.Panicf("should not be reachable for kind %v", mt.kind)
		}
		p.writef("MeasureOutOfLine(value);\n")
	})
	p.writef("}\n")
	p.writef("\n")
	p.writef("void MeasureOutOfLine(const %s& value) {\n", mt.nameToType())
	p.indent(func() {
		switch mt.kind {
		case kUnion:
			mt.writeUnionOutOfLine(p)
		case kStruct:
			mt.writeStructOutOfLine(p)
		case kTable:
			mt.writeTableOutOfLine(p)
		}
	})
	p.writef("}\n")
	if mt.hasHandles {
		p.writef("\n")
		p.writef("void MeasureHandles(const %s& value) {\n", mt.nameToType())
		p.indent(func() {
			switch mt.kind {
			case kStruct:
				for _, member := range mt.members {
					if member.mt.kind == kHandle {
						// TODO(fxb/49488): Conditionally increase for nullable handles.
						p.writef("num_handles_ += 1;\n")
					} else if member.mt.hasHandles {
						p.writef("MeasureHandles(value.%s);\n", fidlcommon.ToSnakeCase(member.name))
					}
				}
			}
		})
		p.writef("}\n")
	}
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
		log.Panic("incorrect fieldKind")
		return ""
	}
}

func (member *measuringTapeMember) guardNullableAccess(p *Printer, fieldMode fieldKind, fn func(derefOp)) {
	if member.mt.nullable {
		p.writef("if (%s) {\n", member.accessor(fieldMode))
		p.indent(func() {
			fn(pointerDeref)
		})
		p.writef("}\n")
	} else {
		fn(directDeref)
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

func (member *measuringTapeMember) writeInvoke(p *Printer, mode invokeKind, fieldMode fieldKind) {
	switch member.mt.kind {
	case kString:
		if mode == inlineAndOutOfLine {
			p.writef("num_bytes_ += sizeof(fidl_string_t);\n")
		}
		member.guardNullableAccess(p, fieldMode, func(op derefOp) {
			p.writef("num_bytes_ += FIDL_ALIGN(%s%slength());\n",
				member.accessor(fieldMode), op)
		})
	case kVector:
		if mode == inlineAndOutOfLine {
			p.writef("num_bytes_ += sizeof(fidl_vector_t);\n")
		}
		member.guardNullableAccess(p, fieldMode, func(op derefOp) {
			if mode == inlineAndOutOfLine {
				if member.mt.elementMt.kind == kHandle ||
					(!member.mt.elementMt.hasOutOfLine && member.mt.elementMt.hasHandles) {
					// TODO(fxb/49488): Conditionally increase for nullable handles.
					p.writef("num_handles_ += %s%ssize() * %d;\n",
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
				p.writef("for (const auto& %s : %s%s) {\n", memberMt.name, deref, member.accessor(fieldMode))
				p.indent(func() {
					memberMt.writeInvoke(p, inlineAndOutOfLine, localVar)
				})
				p.writef("}\n")
			} else {
				p.writef("num_bytes_ += FIDL_ALIGN(%s%ssize() * %d);\n",
					member.accessor(fieldMode), op, member.mt.elementMt.inlineNumBytes)
			}
		})
	case kArray:
		if mode == inlineAndOutOfLine {
			p.writef("num_bytes_ += FIDL_ALIGN(%d);\n", member.mt.inlineNumBytes)
			if member.mt.elementMt.kind == kHandle || (!member.mt.hasOutOfLine && member.mt.hasHandles) {
				// TODO(fxb/49488): Conditionally increase for nullable handles.
				p.writef("num_handles_ += %d;\n", member.mt.inlineNumHandles)
			}
		}
		if member.mt.hasOutOfLine {
			memberMt := measuringTapeMember{
				name: fmt.Sprintf("%s_elem", member.name),
				mt:   member.mt.elementMt,
			}
			p.writef("for (const auto& %s : %s) {\n", memberMt.name, member.accessor(fieldMode))
			p.indent(func() {
				memberMt.writeInvoke(p, outOfLineOnly, localVar)
			})
			p.writef("}\n")
		}
	case kPrimitive:
		if mode == inlineAndOutOfLine {
			p.writef("num_bytes_ += 8;\n")
		}
	case kHandle:
		if mode == inlineAndOutOfLine {
			p.writef("num_bytes_ += 8;\n")
			// TODO(fxb/49488): Conditionally increase for nullable handles.
			p.writef("num_handles_ += 1;\n")
		}
	default:
		p.add(member.mt)
		member.guardNullableAccess(p, fieldMode, func(_ derefOp) {
			switch mode {
			case inlineAndOutOfLine:
				p.writef("Measure(%s);\n", member.accessor(fieldMode))
			case outOfLineOnly:
				p.writef("MeasureOutOfLine(%s);\n", member.accessor(fieldMode))
			}
		})
	}
}

func (mt *MeasuringTape) writeStructOutOfLine(p *Printer) {
	for _, member := range mt.members {
		member.writeInvoke(p, outOfLineOnly, directAccessField)
	}
}

func (mt *MeasuringTape) writeUnionOutOfLine(p *Printer) {
	p.writef("switch (value.Which()) {\n")
	p.indent(func() {
		for _, member := range mt.members {
			p.writef("case %s:\n", mt.memberNameToUnionTag(member.name))
			p.indent(func() {
				member.writeInvoke(p, inlineAndOutOfLine, throughAccessorField)
				p.writef("break;\n")
			})
		}
		p.writef("case %s:\n", mt.memberNameToInvalidUnionTag())
		p.indent(func() {
			p.writef("MaxOut();\n")
			p.writef("break;\n")
		})
	})
	p.writef("}\n")
}

func (mt *MeasuringTape) writeTableOutOfLine(p *Printer) {
	p.writef("int32_t max_ordinal = 0;\n")
	for _, member := range mt.members {
		p.writef("if (value.has_%s()) {\n", fidlcommon.ToSnakeCase(member.name))
		p.indent(func() {
			member.writeInvoke(p, inlineAndOutOfLine, throughAccessorField)
			p.writef("max_ordinal = %d;\n", member.ordinal)
		})
		p.writef("}\n")
	}
	p.writef("num_bytes_ += sizeof(fidl_envelope_t) * max_ordinal;\n")
}

func (mt *MeasuringTape) nameToType() string {
	return fmt.Sprintf("::%s::%s", strings.Join(mt.name.LibraryName().Parts(), "::"), mt.name.DeclarationName())
}

func (mt *MeasuringTape) memberNameToUnionTag(memberName string) string {
	return fmt.Sprintf("%s::Tag::k%s", mt.nameToType(), fidlcommon.ToUpperCamelCase(memberName))
}

func (mt *MeasuringTape) memberNameToInvalidUnionTag() string {
	return fmt.Sprintf("%s::Tag::Invalid", mt.nameToType())
}
