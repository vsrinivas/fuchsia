// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"io"
)

// BasicPresenter is a presenter to output very basic uncolored output
type BasicPresenter struct {
	enableColor   bool
	lineUsedColor bool
	output        io.Writer
}

func NewBasicPresenter(output io.Writer, enableColor bool) *BasicPresenter {
	return &BasicPresenter{output: output, enableColor: enableColor}
}

func (b *BasicPresenter) WriteLine(res OutputLine) {
	if b.enableColor {
		// Reset color use at start of line.
		b.lineUsedColor = false
		// No matter way, reset color/bold at the end of a line
		defer func() {
			if b.lineUsedColor {
				fmt.Fprintf(b.output, "\033[0m")
			}
		}()
	}
	if hdr, ok := res.header.(logHeader); ok {
		fmt.Fprintf(b.output, "[%.3f] %05d.%05d> ", hdr.time, hdr.process, hdr.thread)
	}
	if hdr, ok := res.header.(sysLogHeader); ok {
		fmt.Fprintf(b.output, "[%012.6f][%d][%d][%s] ", hdr.time, hdr.process, hdr.thread, hdr.tags)
	}
	for _, token := range res.line {
		token.Accept(b)
	}
	fmt.Fprint(b.output, "\n")
}

// Start tells the basic presenter to start writing to its output
func (b *BasicPresenter) Start(input <-chan OutputLine) {
	for res := range input {
		b.WriteLine(res)
	}
}

func printSrcLoc(out io.Writer, loc SourceLocation, info AddressInfo) {
	modRelAddr := info.addr - info.seg.vaddr + info.seg.modRelAddr
	if !loc.function.IsEmpty() {
		fmt.Fprintf(out, "%s at ", loc.function)
	}
	if !loc.file.IsEmpty() {
		fmt.Fprintf(out, "%s:%d", loc.file, loc.line)
	} else {
		fmt.Fprintf(out, "<%s>+0x%x", info.mod.name, modRelAddr)
	}
}

func (b *BasicPresenter) VisitBt(node *BacktraceElement) {
	if len(node.info.locs) == 0 {
		printSrcLoc(b.output, SourceLocation{}, node.info)
	}
	for i, loc := range node.info.locs {
		printSrcLoc(b.output, loc, node.info)
		if i != len(node.info.locs)-1 {
			fmt.Fprint(b.output, " inlined from ")
		}
	}
}

func (b *BasicPresenter) VisitPc(node *PCElement) {
	if len(node.info.locs) > 0 {
		printSrcLoc(b.output, node.info.locs[0], node.info)
	} else {
		printSrcLoc(b.output, SourceLocation{}, node.info)
	}
}

func (b *BasicPresenter) VisitColor(node *ColorCode) {
	if b.enableColor {
		b.lineUsedColor = true
		fmt.Fprintf(b.output, "\033[%dm", node.color)
	}
}

func (b *BasicPresenter) VisitText(node *Text) {
	fmt.Fprint(b.output, node.text)
}

func (b *BasicPresenter) VisitReset(elem *ResetElement) {
	fmt.Fprintf(b.output, "{{{reset}}}")
}

func (b *BasicPresenter) VisitModule(node *ModuleElement) {
	fmt.Fprintf(b.output, "{{{module:%s:%s:%d}}}", node.mod.build, node.mod.name, node.mod.id)
}

func (b *BasicPresenter) VisitMapping(node *MappingElement) {
	fmt.Fprintf(b.output, "{{{mmap:0x%x:0x%x:load:%d:%s:0x%x}}}", node.seg.vaddr, node.seg.size, node.seg.mod, node.seg.flags,
		node.seg.modRelAddr)
}
