// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"fmt"

	"fuchsia.googlesource.com/tools/logger"
)

// TODO (jakehehrlich): LineSource is now a part of the public interface. This is needed to
// allow for the proper construction of triggers since triggers need to know
// where a triggering element came from. Right now this is just an empty
// interface. It would be nice if the user could do soemthing other than cast this.
type LineSource interface{}

type Process uint64

type DummySource struct{}

type lineHeader interface{}

type logHeader struct {
	time    float64
	process uint64
	thread  uint64
}

type sysLogHeader struct {
	time    float64
	process uint64
	thread  uint64
	tags    string
}

type LogLine struct {
	lineno uint64
	header lineHeader
	source LineSource
}

type InputLine struct {
	LogLine
	msg string
}

// Later this will be more general.
type Segment struct {
	Mod        uint64 `json:"mod"`
	Vaddr      uint64 `json:"vaddr"`
	Size       uint64 `json:"size"`
	Flags      string `json:"flags"`
	ModRelAddr uint64 `json:"modRelAddr"`
}

type addressInfo struct {
	locs []SourceLocation
	mod  Module
	seg  Segment
	addr uint64
}

type Module struct {
	Name  string `json:"name"`
	Build string `json:"build"`
	Id    uint64 `json:"id"`
}

type OutputLine struct {
	LogLine
	line []Node
}

type missingObjError struct {
	name    string
	buildid string
	err     error
}

func (m *missingObjError) Error() string {
	return fmt.Sprintf("could not find file for module %s with build ID %s: %v", m.name, m.buildid, m.err)
}

// Filter represents the state needed to process a log.
type Filter struct {
	// handles for llvm-symbolizer
	symbolizer Symbolizer
	// Symbolizer context
	symContext        mappingStore
	modules           map[uint64]Module
	modNamesByBuildID map[string]string
	// Symbolizer repository
	repo *SymbolizerRepo
}

// TODO (jakehehrlich): Consider making FindInfoForAddress private.

// FindInfoForAddress takes a process an in memory address and converts it to a source location.
func (s *Filter) findInfoForAddress(vaddr uint64) (addressInfo, error) {
	info := addressInfo{addr: vaddr}
	seg := s.symContext.find(vaddr)
	if seg == nil {
		return info, fmt.Errorf("could not find segment that covers 0x%x", vaddr)
	}
	info.seg = *seg
	if mod, ok := s.modules[info.seg.Mod]; ok {
		info.mod = mod
	} else {
		return info, fmt.Errorf("could not find module for 0x%x", vaddr)
	}
	modRelAddr := vaddr - seg.Vaddr + seg.ModRelAddr
	mod, ok := s.modules[seg.Mod]
	if !ok {
		return info, fmt.Errorf("could not find module with module ID %d", seg.Mod)
	}
	modPath, err := s.repo.GetBuildObject(mod.Build)
	if err != nil {
		out := &missingObjError{mod.Name, mod.Build, err}
		return info, out
	}
	result := <-s.symbolizer.FindSrcLoc(modPath, mod.Build, modRelAddr)
	if result.Err != nil {
		return info, fmt.Errorf("in module %s with build ID %s: %v", mod.Name, mod.Build, result.Err)
	}
	info.locs = result.Locs
	return info, nil
}

// NewFilter creates a new filter
func NewFilter(repo *SymbolizerRepo, symbo Symbolizer) *Filter {
	return &Filter{
		modules:           make(map[uint64]Module),
		modNamesByBuildID: make(map[string]string),
		repo:              repo,
		symbolizer:        symbo,
	}
}

// Reset resets the filter so that it can work for a new process
func (s *Filter) reset() {
	s.modules = make(map[uint64]Module)
	s.symContext.clear()
}

// AddModule updates the filter state to inform it of a new module
func (s *Filter) addModule(m Module) error {
	var err error
	// Flag odd build IDs.
	if modName, ok := s.modNamesByBuildID[m.Build]; ok {
		if modName != m.Name {
			err = fmt.Errorf("found two modules named %s and %s with the same build ID of %s", modName, m.Name, m.Build)
		}
	}
	// Keep track of modules by build ID.
	s.modNamesByBuildID[m.Build] = m.Name
	s.modules[m.Id] = m
	return err
}

// AddSegment updates the filter state to inform it of a new memory mapped location.
func (s *Filter) addSegment(seg Segment) {
	s.symContext.add(seg)
}

// Start tells the filter to start consuming input and produce output.
func (f *Filter) Start(ctx context.Context, input <-chan InputLine) <-chan OutputLine {
	out := make(chan OutputLine)
	parseLine := GetLineParser()
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
				if res.line = parseLine(elem.msg); res.line == nil {
					res.line = []Node{&Text{text: elem.msg}}
				}
				// Update AST with source locations.
				for _, token := range res.line {
					token.Accept(&filterVisitor{filter: f, lineno: elem.lineno, ctx: ctx, source: elem.source})
				}
				res.LogLine = elem.LogLine
				out <- res
			}
		}
	}()
	return out
}

type filterVisitor struct {
	filter *Filter
	lineno uint64
	ctx    context.Context
	source LineSource
}

func (f *filterVisitor) warn(err error) {
	logger.Warningf(f.ctx, "on line %d: %v", f.lineno, err)
}

func (f *filterVisitor) VisitBt(elem *BacktraceElement) {
	info, err := f.filter.findInfoForAddress(elem.vaddr)
	if err != nil {
		// Don't be noisy about missing objects.
		if _, ok := err.(*missingObjError); !ok {
			f.warn(err)
		}
	}
	elem.info = info
}

func (f *filterVisitor) VisitPc(elem *PCElement) {
	info, err := f.filter.findInfoForAddress(elem.vaddr)
	if err != nil {
		// Don't be noisy about missing objects.
		if _, ok := err.(*missingObjError); !ok {
			f.warn(err)
		}
	}
	elem.info = info
}

func (f *filterVisitor) VisitColor(group *ColorCode) {

}

func (f *filterVisitor) VisitText(_ *Text) {
	// This must be implemented in order to meet the interface but it has no effect.
	// This visitor is supposed to do all of the non-parsing parts of constructing the AST.
	// There is nothing to do for Text however.
}

func (f *filterVisitor) VisitDump(elem *DumpfileElement) {
	// Defensive copies must be made here for two reasons:
	//   1) We don't want the trigger handler to change internal state
	//   2) The trigger handler is allowed to store/process the context
	//      at a later date and we might have modified either of these
	//      in the interim.
	segs := f.filter.symContext.GetSegments()
	mods := []Module{}
	for _, mod := range f.filter.modules {
		mods = append(mods, mod)
	}
	elem.context = &TriggerContext{Source: f.source, Mods: mods, Segs: segs}
}

func (f *filterVisitor) VisitReset(elem *ResetElement) {
	// TODO: Check if Reset had an effect and output that a pid reuse occured.
	f.filter.reset()
}

func (f *filterVisitor) VisitModule(elem *ModuleElement) {
	err := f.filter.addModule(elem.mod)
	if err != nil {
		f.warn(err)
	}
}

func (f *filterVisitor) VisitMapping(elem *MappingElement) {
	f.filter.addSegment(elem.seg)
}
