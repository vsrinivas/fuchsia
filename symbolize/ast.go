// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

// PresentationVisistor is a visitor for presentables
type NodeVisitor interface {
	VisitBt(elem *BacktraceElement)
	VisitPc(elem *PCElement)
	VisitColor(node *ColorGroup)
	VisitText(node *Text)
	VisitReset(elem *ResetElement)
	VisitModule(elem *ModuleElement)
	VisitMapping(elem *MappingElement)
	VisitGroup(elem *PresentationGroup)
}
type Node interface {
	Accept(visitor NodeVisitor)
}

// PresentationGroup represents a sequence of presentable parts of a line
type PresentationGroup struct {
	children []Node
}

func (p *PresentationGroup) Accept(visitor NodeVisitor) {
	visitor.VisitGroup(p)
}

// SourceLocation represents a location in a source file
type SourceLocation struct {
	file     string
	line     int
	function string
}

type BacktraceElement struct {
	vaddr uint64
	num   uint64
	locs  []SourceLocation
}

func (b *BacktraceElement) Accept(visitor NodeVisitor) {
	visitor.VisitBt(b)
}

// PcElement is an AST node representing a pc element in the markup
type PCElement struct {
	vaddr uint64
	loc   SourceLocation
}

func (p *PCElement) Accept(visitor NodeVisitor) {
	visitor.VisitPc(p)
}

// ColorGroup is an AST node representing a colored part of the markup
type ColorGroup struct {
	color    uint64
	children []Node
}

func (c *ColorGroup) Accept(visitor NodeVisitor) {
	visitor.VisitColor(c)
}

// Text represents text between the special parts of the markup
type Text struct {
	text string
}

func (t *Text) Accept(visitor NodeVisitor) {
	visitor.VisitText(t)
}

type ResetElement struct{}

func (r *ResetElement) Accept(visitor NodeVisitor) {
	visitor.VisitReset(r)
}

// ModuleElement represents a module element in the markup
type ModuleElement struct {
	mod Module
}

func (m *ModuleElement) Accept(visitor NodeVisitor) {
	visitor.VisitModule(m)
}

// MappingElement represents an mmap element in the markup
type MappingElement struct {
	seg Segment
}

func (s *MappingElement) Accept(visitor NodeVisitor) {
	visitor.VisitMapping(s)
}
