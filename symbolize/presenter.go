// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bytes"
	"fmt"
	"io"
	"strings"
)

// BasicPresenter is a presenter to output very basic uncolored output
type BasicPresenter struct {
	enableColor   bool
	lineUsedColor bool
	hasOutput     bool
	output        io.Writer
	lazyPrintBuf  bytes.Buffer
}

func NewBasicPresenter(output io.Writer, enableColor bool) *BasicPresenter {
	return &BasicPresenter{output: output, enableColor: enableColor}
}

func (b *BasicPresenter) flushLazyOutput() {
	b.lazyPrintBuf.WriteTo(b.output)
	b.lazyPrintBuf.Reset()
	b.hasOutput = true
}

func (b *BasicPresenter) resetLineState() {
	b.lazyPrintBuf.Reset()
	b.lineUsedColor = false
	b.hasOutput = false
}

// Used to print things which should only be output if something else
// is output.
func (b *BasicPresenter) printfLazy(fmtStr string, args ...interface{}) {
	if b.hasOutput {
		fmt.Fprintf(b.output, fmtStr, args...)
	} else {
		fmt.Fprintf(&b.lazyPrintBuf, fmtStr, args...)
	}
}

// TODO (jakehehrlich): The handling of space here is a bit odd. We should
// decide if it makes sense to just Trim all messages in the parser or at
// some other stage so that this sort of issue can be avoided. The caveat
// is that with regard to space at the begining and end of lines we don't
// as faithfully output what the user gave us.

// Used to print things that should always be output but treats space lazily.
// If space were not treated lazily then a stray trailing space or space before
// a contextual element could cause the whole line to print out despite no
// information being added.
func (b *BasicPresenter) printfForce(fmtStr string, args ...interface{}) {
	if len(strings.TrimSpace(fmtStr)) == 0 {
		b.printfLazy(fmtStr)
		return
	}
	if !b.hasOutput {
		b.flushLazyOutput()
	}
	fmt.Fprintf(b.output, fmtStr, args...)
}

func (b *BasicPresenter) WriteLine(res OutputLine) {
	b.resetLineState()
	if b.enableColor {
		// No matter way, reset color/bold at the end of a line
		defer func() {
			if b.lineUsedColor {
				// Just because a color is printed out doesn't mean
				// everything should be printed out so print this color
				// out lazily.
				b.printfLazy("\033[0m")
			}
		}()
	}
	if hdr, ok := res.header.(logHeader); ok {
		// The header only needs to be printed out if the actual content needs to be printed out
		// so we should lazily print it out.
		b.printfLazy("[%.3f] %05d.%05d> ", hdr.time, hdr.process, hdr.thread)
	}
	if hdr, ok := res.header.(sysLogHeader); ok {
		b.printfLazy("[%012.6f][%d][%d][%s] ", hdr.time, hdr.process, hdr.thread, hdr.tags)
	}
	for _, token := range res.line {
		token.Accept(b)
	}
	// This will be handled lazily no matter what but I use printfLazy to more directly say that.
	// If this newline were always printed out then every line would always print out which is
	// exactly what we're trying to avoid.
	b.printfLazy("\n")
}

// Start tells the basic presenter to start writing to its output
func (b *BasicPresenter) Start(input <-chan OutputLine) {
	for res := range input {
		b.WriteLine(res)
	}
}

func (b *BasicPresenter) printSrcLoc(loc SourceLocation, info addressInfo) {
	modRelAddr := info.addr - info.seg.Vaddr + info.seg.ModRelAddr
	if !loc.function.IsEmpty() {
		b.printfForce("%s at ", loc.function)
	}
	if !loc.file.IsEmpty() {
		b.printfForce("%s:%d", loc.file, loc.line)
	} else {
		b.printfForce("<%s>+0x%x", info.mod.Name, modRelAddr)
	}
}

func (b *BasicPresenter) VisitBt(node *BacktraceElement) {
	if len(node.info.locs) == 0 {
		b.printSrcLoc(SourceLocation{}, node.info)
	}
	for i, loc := range node.info.locs {
		b.printSrcLoc(loc, node.info)
		if i != len(node.info.locs)-1 {
			b.printfForce(" inlined from ")
		}
	}
}

func (b *BasicPresenter) VisitPc(node *PCElement) {
	if len(node.info.locs) > 0 {
		b.printSrcLoc(node.info.locs[0], node.info)
	} else {
		b.printSrcLoc(SourceLocation{}, node.info)
	}
}

func (b *BasicPresenter) VisitColor(node *ColorCode) {
	if b.enableColor {
		b.lineUsedColor = true
		b.printfLazy("\033[%dm", node.color)
	}
}

func (b *BasicPresenter) VisitText(node *Text) {
	b.printfForce(node.text)
}

func (b *BasicPresenter) VisitDump(elem *DumpfileElement) {
	b.printfForce("{{{dumpfile:%s:%s}}}", elem.sinkType, elem.name)
}

func (b *BasicPresenter) VisitReset(elem *ResetElement) {
	// Don't output {{{reset}}} at all
}

func (b *BasicPresenter) VisitModule(node *ModuleElement) {
	b.printfLazy("{{{module:%s:%s:%d}}}", node.mod.Build, node.mod.Name, node.mod.Id)
}

func (b *BasicPresenter) VisitMapping(node *MappingElement) {
	b.printfLazy("{{{mmap:0x%x:0x%x:load:%d:%s:0x%x}}}", node.seg.Vaddr, node.seg.Size, node.seg.Mod, node.seg.Flags,
		node.seg.ModRelAddr)
}
