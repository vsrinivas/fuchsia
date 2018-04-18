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
	output io.Writer
}

func NewBasicPresenter(output io.Writer) *BasicPresenter {
	return &BasicPresenter{output}
}

func (b *BasicPresenter) WriteLine(res OutputLine) {
	fmt.Fprintf(b.output, "[%.3f] %d.%d> ", res.time, res.process, res.thread)
	res.line.Accept(b)
	fmt.Fprint(b.output, "\n")
}

// Start tells the basic presenter to start writing to its output
func (b *BasicPresenter) Start(input <-chan OutputLine) {
	for res := range input {
		b.WriteLine(res)
	}
}

func (b *BasicPresenter) VisitPc(node *PCElement) {
	fmt.Fprintf(b.output, "%s:%d", node.loc.file, node.loc.line)
}

func (b *BasicPresenter) VisitColor(node *ColorGroup) {
	// The basic presenter ignores color
	for _, child := range node.children {
		child.Accept(b)
	}
}

func (b *BasicPresenter) VisitText(node *Text) {
	fmt.Fprint(b.output, node.text)
}

func (b *BasicPresenter) VisitReset(elem *ResetElement) {
	fmt.Fprintf(b.output, "{{{reset}}}")
}

func (b *BasicPresenter) VisitGroup(group *PresentationGroup) {
	for _, child := range group.children {
		child.Accept(b)
	}
}

func (b *BasicPresenter) VisitModule(node *ModuleElement) {
	fmt.Fprintf(b.output, "{{{module:%s:%s:%d}}}", node.mod.build, node.mod.name, node.mod.id)
}

func (b *BasicPresenter) VisitMapping(node *MappingElement) {
	fmt.Fprintf(b.output, "{{{mmap:0x%x:%d:load:%d:%s:0x%x}}}", node.seg.vaddr, node.seg.size, node.seg.mod, node.seg.flags,
		node.seg.modRelAddr)
}
