// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

// Line represents the content of a single log line
type Line interface {
	Accept(visitor LineVisitor)
}

// Presentable is the interface of any presentable part of a line
type Presentable interface {
	Accept(visitor PresentationVisitor)
}

// PresentationGroup represents a sequence of presentable parts of a line
type PresentationGroup struct {
	children []Presentable
}

func (p *PresentationGroup) Accept(visitor LineVisitor) {
	visitor.VisitGroup(p)
}

// SourceLocation represents a location in a source file
type SourceLocation struct {
	file     string
	line     int
	function string
}

// PcElement is an AST node representing a pc element in the markup
type PCElement struct {
	vaddr uint64
	loc   SourceLocation
}

func (p *PCElement) Accept(visitor PresentationVisitor) {
	visitor.VisitPc(p)
}

// ColorGroup is an AST node representing a colored part of the markup
type ColorGroup struct {
	color    uint64
	children []Presentable
}

func (c *ColorGroup) Accept(visitor PresentationVisitor) {
	visitor.VisitColor(c)
}

// Text represents text between the special parts of the markup
type Text struct {
	text string
}

func (t *Text) Accept(visitor PresentationVisitor) {
	visitor.VisitText(t)
}

type ResetElement struct{}

func (r *ResetElement) Accept(visitor LineVisitor) {
	visitor.VisitReset(r)
}

// ModuleElement represents a module element in the markup
type ModuleElement struct {
	mod Module
}

func (m *ModuleElement) Accept(visitor LineVisitor) {
	visitor.VisitModule(m)
}

// MappingElement represents an mmap element in the markup
type MappingElement struct {
	seg Segment
}

func (s *MappingElement) Accept(visitor LineVisitor) {
	visitor.VisitMapping(s)
}

// PresentationVisistor is a visitor for presentables
type PresentationVisitor interface {
	VisitPc(elem *PCElement)
	VisitColor(node *ColorGroup)
	VisitText(node *Text)
}

// LineVisitor is a visitor for lines
type LineVisitor interface {
	VisitReset(elem *ResetElement)
	VisitModule(elem *ModuleElement)
	VisitMapping(elem *MappingElement)
	VisitGroup(elem *PresentationGroup)
}
