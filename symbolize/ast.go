// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"encoding/json"
	"fmt"
)

// TODO (jakehehrlich): Add methods to make these node types usable

// NodeVisitor is a visitor for all Node types
type NodeVisitor interface {
	VisitBt(elem *BacktraceElement)
	VisitPc(elem *PCElement)
	VisitColor(node *ColorCode)
	VisitText(node *Text)
	VisitDump(elem *DumpfileElement)
	VisitReset(elem *ResetElement)
	VisitModule(elem *ModuleElement)
	VisitMapping(elem *MappingElement)
}
type Node interface {
	Accept(visitor NodeVisitor)
}

// OptionalString implements possibly missing strings from llvm-symbolizer
type OptStr struct {
	val *string
}

func EmptyOptStr() OptStr {
	return OptStr{}
}

func NewOptStr(s string) OptStr {
	return OptStr{&s}
}

func (o *OptStr) Unwrap(def string) string {
	if o.val != nil {
		return *o.val
	}
	return def
}

func (o OptStr) IsEmpty() bool {
	return o.val == nil
}

func (o OptStr) String() string {
	return o.Unwrap("??")
}

func (o OptStr) MarshalJSON() ([]byte, error) {
	return json.Marshal(o.String())
}

func (o OptStr) Format(f fmt.State, c rune) {
	f.Write([]byte(o.String()))
}

// SourceLocation represents a location in a source file
type SourceLocation struct {
	file     OptStr
	line     int
	function OptStr
}

type BacktraceElement struct {
	vaddr uint64
	num   uint64
	info  addressInfo
}

func (b *BacktraceElement) Accept(visitor NodeVisitor) {
	visitor.VisitBt(b)
}

// PcElement is an AST node representing a pc element in the markup
type PCElement struct {
	vaddr uint64
	info  addressInfo
}

func (p *PCElement) Accept(visitor NodeVisitor) {
	visitor.VisitPc(p)
}

// TODO(jakehehrlich): Make this semantic rather than literal (e.g. keep track of color/bold information directly)
// ColorCode is an AST node representing a colored part of the markup
type ColorCode struct {
	color uint64
}

func (c *ColorCode) Accept(visitor NodeVisitor) {
	visitor.VisitColor(c)
}

// Text represents text between the special parts of the markup
type Text struct {
	text string
}

func (t *Text) Accept(visitor NodeVisitor) {
	visitor.VisitText(t)
}

type DumpfileElement struct {
	sinkType string
	name     string
	context  *TriggerContext
}

func (d *DumpfileElement) Context() TriggerContext {
	return *d.context
}

func (d *DumpfileElement) SinkType() string {
	return d.sinkType
}

func (d *DumpfileElement) Name() string {
	return d.name
}

func (d *DumpfileElement) Accept(visitor NodeVisitor) {
	visitor.VisitDump(d)
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
