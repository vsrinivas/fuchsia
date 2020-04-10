// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"bytes"
	"fmt"
	"log"
	"strings"

	fidlcommon "fidl/compiler/backend/common"
)

type Printer struct {
	buf     *bytes.Buffer
	level   int
	toPrint []*MeasuringTape
	done    map[*MeasuringTape]bool
}

func NewPrinter(mt *MeasuringTape, buf *bytes.Buffer) *Printer {
	return &Printer{
		buf:     buf,
		level:   1,
		toPrint: []*MeasuringTape{mt},
		done:    make(map[*MeasuringTape]bool),
	}
}

const topOfFile = `// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// File is automatically generated; do not modify.
// See tools/fidl/measure-tape/README.md

#include <lib/ui/scenic/cpp/commands_sizing.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl.h>
#include <zircon/types.h>

namespace scenic {

namespace {

class MeasuringTape {
 public:
  MeasuringTape() = default;
`

const bottomOfFile = `
  CommandSize Done() {
    if (maxed_out_) {
      return CommandSize(ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES);
    }
    return CommandSize(num_bytes_, num_handles_);
  }

private:
  void MaxOut() { maxed_out_ = true; }

  bool maxed_out_ = false;
  int64_t num_bytes_ = 0;
  int64_t num_handles_ = 0;
};

}  // namespace

CommandSize MeasureCommand(const fuchsia::ui::scenic::Command& command) {
  MeasuringTape tape;
  tape.Measure(command);
  return tape.Done();
}

}  // namespace scenic
`

func (p *Printer) Write() {
	p.buf.WriteString(topOfFile)
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
	p.buf.WriteString(bottomOfFile)
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
)

type derefOp int

const (
	_ derefOp = iota
	directDeref
	pointerDeref
)

func (fieldMode fieldKind) String() string {
	switch fieldMode {
	case directAccessField:
		return ""
	case throughAccessorField:
		return "()"
	default:
		log.Panic("incorrect fieldKind")
		return ""
	}
}

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
		p.writef("if (value.%s%s) {\n", fieldMode, fidlcommon.ToSnakeCase(member.name))
		p.indent(func() {
			fn(pointerDeref)
		})
		p.writef("}\n")
	} else {
		fn(directDeref)
	}
}

func (member *measuringTapeMember) writeInvoke(p *Printer, mode invokeKind, fieldMode fieldKind) {
	switch member.mt.kind {
	case kString:
		if mode == inlineAndOutOfLine {
			p.writef("num_bytes_ += sizeof(fidl_string_t);\n")
		}
		member.guardNullableAccess(p, fieldMode, func(op derefOp) {
			p.writef("num_bytes_ += FIDL_ALIGN(value.%s%s%slength());\n",
				fieldMode, fidlcommon.ToSnakeCase(member.name), op)
		})
	case kVector:
		// TODO(fxb/49480): Support measuring vectors.
		p.writef("// TODO: vectors are not measured yet.\n")
		p.writef("MaxOut();\n")
	case kArray:
		// TODO(fxb/49480): Support measuring arrays.
		p.writef("// TODO: arrays are not measured yet.\n")
		p.writef("MaxOut();\n")
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
				p.writef("Measure(value.%s%s);\n", fidlcommon.ToSnakeCase(member.name), fieldMode)
			case outOfLineOnly:
				p.writef("MeasureOutOfLine(value.%s%s);\n", fidlcommon.ToSnakeCase(member.name), fieldMode)
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
		p.writef("if (has_%s()) {\n", fidlcommon.ToSnakeCase(member.name))
		p.indent(func() {
			member.writeInvoke(p, inlineAndOutOfLine, throughAccessorField)
			p.writef("max_ordinal = %d;\n", member.ordinal)
		})
		p.writef("}\n")
	}
	p.writef("num_bytes += sizeof(fidl_envelope_t) * max_ordinal;\n")
}

func (mt *MeasuringTape) nameToType() string {
	return fmt.Sprintf("%s::%s", strings.Join(mt.name.LibraryNameParts(), "::"), mt.name.DeclarationName())
}

func (mt *MeasuringTape) memberNameToUnionTag(memberName string) string {
	return fmt.Sprintf("%s::Tag::k%s", mt.nameToType(), fidlcommon.ToUpperCamelCase(memberName))
}

func (mt *MeasuringTape) memberNameToInvalidUnionTag() string {
	return fmt.Sprintf("%s::Tag::Invalid", mt.nameToType())
}
