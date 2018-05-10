// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"fmt"
	"log"
)

type lineSource interface{}

type process uint64

type dummySource struct{}

type lineHeader interface{}

type logHeader struct {
	time    float64
	process uint64
	thread  uint64
}

type LogLine struct {
	header lineHeader
	source lineSource
}

type InputLine struct {
	LogLine
	msg string
}

// Later this will be more general.
type Segment struct {
	mod        uint64
	vaddr      uint64
	size       uint64
	flags      string
	modRelAddr uint64
}

type AddressInfo struct {
	locs []SourceLocation
	mod  Module
	seg  Segment
	addr uint64
}

type Module struct {
	name  string
	build string
	id    uint64
}

type OutputLine struct {
	LogLine
	line Node
}

// Filter represents the state needed to process a log.
type Filter struct {
	// handles for llvm-symbolizer
	symbolizer Symbolizer
	// Symbolizer context
	symContext MappingStore
	modules    map[uint64]Module
	// Symbolizer repository
	repo *SymbolizerRepo
}

// FindInfoForAddress takes a process an in memory address and converts it to a source location.
func (s *Filter) FindInfoForAddress(vaddr uint64) (AddressInfo, error) {
	info := AddressInfo{addr: vaddr}
	seg := s.symContext.Find(vaddr)
	if seg == nil {
		return info, fmt.Errorf("could not find segment that covers 0x%x", vaddr)
	}
	info.seg = *seg
	if mod, ok := s.modules[info.seg.mod]; ok {
		info.mod = mod
	} else {
		return info, fmt.Errorf("could not find module for 0x%x", vaddr)
	}
	modRelAddr := vaddr - seg.vaddr + seg.modRelAddr
	modPath := s.repo.GetBuildObject(s.modules[seg.mod].build)
	result := <-s.symbolizer.FindSrcLoc(modPath, modRelAddr)
	if result.Err != nil {
		return info, result.Err
	}
	info.locs = result.Locs
	return info, nil
}

// NewFilter creates a new filter
func NewFilter(repo *SymbolizerRepo, symbo Symbolizer) *Filter {
	return &Filter{
		modules:    make(map[uint64]Module),
		repo:       repo,
		symbolizer: symbo,
	}
}

// Reset resets the filter so that it can work for a new process
func (s *Filter) Reset() {
	s.modules = make(map[uint64]Module)
	s.symContext.Clear()
}

// AddModule updates the filter state to inform it of a new module
func (s *Filter) AddModule(m Module) {
	s.modules[m.id] = m
}

// AddSegment updates the filter state to inform it of a new memory mapped location.
func (s *Filter) AddSegment(seg Segment) {
	s.symContext.Add(seg)
}

// Start tells the filter to start consuming input and produce output.
func (f *Filter) Start(ctx context.Context, input <-chan InputLine) <-chan OutputLine {
	out := make(chan OutputLine)
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			case elem, ok := <-input:
				if !ok {
					return
				}
				var res OutputLine
				if res.line = ParseLine(elem.msg); res.line == nil {
					res.line = &Text{text: elem.msg}
					log.Printf("warning malformed input %s", elem.msg)
				}
				// Update AST with source locations.
				res.line.Accept(&FilterVisitor{f})
				res.LogLine = elem.LogLine
				out <- res
			}
		}
	}()
	return out
}

type FilterVisitor struct {
	filter *Filter
}

func (f *FilterVisitor) VisitBt(elem *BacktraceElement) {
	info, err := f.filter.FindInfoForAddress(elem.vaddr)
	if err != nil {
		log.Printf("warning: %v", err)
	}
	elem.info = info
}

func (f *FilterVisitor) VisitPc(elem *PCElement) {
	info, err := f.filter.FindInfoForAddress(elem.vaddr)
	if err != nil {
		log.Printf("warning: %v", err)
	}
	elem.info = info
}

func (f *FilterVisitor) VisitColor(group *ColorGroup) {
	for _, child := range group.children {
		child.Accept(f)
	}
}

func (f *FilterVisitor) VisitText(_ *Text) {
	// This must be implemented in order to meet the interface but it has no effect.
	// This visitor is supposed to do all of the non-parsing parts of constructing the AST.
	// There is nothing to do for Text however.
}

func (f *FilterVisitor) VisitReset(elem *ResetElement) {
	f.filter.Reset()
}

func (f *FilterVisitor) VisitGroup(group *PresentationGroup) {
	for _, child := range group.children {
		child.Accept(f)
	}
}

func (f *FilterVisitor) VisitModule(elem *ModuleElement) {
	f.filter.AddModule(elem.mod)
}

func (f *FilterVisitor) VisitMapping(elem *MappingElement) {
	f.filter.AddSegment(elem.seg)
}
