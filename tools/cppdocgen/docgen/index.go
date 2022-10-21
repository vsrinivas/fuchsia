// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"log"
	"path"
	"sort"
)

type IndexSettings struct {
	// Path to the build directory where the clang-doc paths are relative to.
	BuildDir string

	// Names of the header files we want to index.
	Headers map[string]struct{}
}

func (s IndexSettings) ShouldIndexInHeader(hdrName string) bool {
	_, found := s.Headers[hdrName]
	return found
}

// HeaderPath returns the path for the given build-dir-relative (what clang-doc generates) header
// path.
func (s IndexSettings) HeaderPath(h string) string {
	return path.Join(s.BuildDir, h)
}

// Flattened version of everything stored in this library.
type Index struct {
	// All nonmember functions/records in all non-anonymous namespaces, indexed by the USR/Name.
	// Since functions overloads may have name collisions, FunctionNames may not include
	// everything (it currently stores the last one of that name encountered). FunctionUsrs
	// will include everything since Clang will generate a unique USR for each function.
	//
	// This could be enhanced in the future if needed to map a name to a list of functions. But
	// currently we only use the name index to generate links where there can only be one
	// destination anyway.
	FunctionUsrs  map[string]*clangdoc.FunctionInfo
	FunctionNames map[string]*clangdoc.FunctionInfo

	// Records (classes, structs, and unions), indexed by the Name/USR.
	RecordNames map[string]*clangdoc.RecordInfo
	RecordUsrs  map[string]*clangdoc.RecordInfo

	// All unique header files in this library indexed by their file name.
	Headers map[string]*Header

	// All unique preprocessor defines in this library indexed by their name.
	Defines map[string]*Define

	// All enums indexed by their name.
	Enums map[string]*clangdoc.EnumInfo
}

// Returns a sorted list of all functions.
func (index Index) AllFunctions() []*clangdoc.FunctionInfo {
	result := make([]*clangdoc.FunctionInfo, 0, len(index.FunctionUsrs))
	for _, fn := range index.FunctionUsrs {
		result = append(result, fn)
	}
	sort.Sort(functionByName(result))
	return result
}

// Returns a sorted list of all records.
func (index Index) AllRecords() []*clangdoc.RecordInfo {
	result := make([]*clangdoc.RecordInfo, 0, len(index.RecordUsrs))
	for _, r := range index.RecordUsrs {
		result = append(result, r)
	}
	sort.Sort(recordByName(result))
	return result
}

// AllDefines returns a sorted list of all defines.
func (index Index) AllDefines() []*Define {
	result := make([]*Define, 0, len(index.Defines))
	for _, d := range index.Defines {
		result = append(result, d)
	}
	sort.Sort(defineByName(result))
	return result
}

func (index *Index) HeaderForFileName(file string) *Header {
	header := index.Headers[file]
	if header == nil {
		// New item, init Header struct.
		header = &Header{Name: file}
		index.Headers[file] = header
	}
	return header
}

type FunctionGroup struct {
	// Nonempty when an explicit title is given. Empty means just use the first name.
	ExplicitTitle string

	Funcs []*clangdoc.FunctionInfo
}

// Interface for sorting a function group by the first function's name.
type functionGroupByTitle []*FunctionGroup

func (f functionGroupByTitle) Len() int {
	return len(f)
}
func (f functionGroupByTitle) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f functionGroupByTitle) Less(i, j int) bool {
	getName := func(g *FunctionGroup) string {
		if len(g.ExplicitTitle) > 0 {
			return g.ExplicitTitle
		}
		return g.Funcs[0].Name
	}
	return getName(f[i]) < getName(f[j])
}

// Interface for sorting an enum list by name.
type enumByName []*clangdoc.EnumInfo

func (f enumByName) Len() int {
	return len(f)
}
func (f enumByName) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f enumByName) Less(i, j int) bool {
	return f[i].Name < f[j].Name
}

type DefineGroup struct {
	// Nonempty when an explicit title is given. Empty means just use the first name.
	ExplicitTitle string

	Defines []*Define
}

// Interface for sorting a define group by the first define's name.
type defineGroupByTitle []*DefineGroup

func (f defineGroupByTitle) Len() int {
	return len(f)
}
func (f defineGroupByTitle) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f defineGroupByTitle) Less(i, j int) bool {
	getName := func(g *DefineGroup) string {
		if len(g.ExplicitTitle) > 0 {
			return g.ExplicitTitle
		}
		return g.Defines[0].Name
	}
	return getName(f[i]) < getName(f[j])
}

func indexFunction(settings IndexSettings, index *Index, f *clangdoc.FunctionInfo) {
	if len(f.Location) == 0 {
		fmt.Printf("WARNING: Function %s does not have a declaration location.\n", f.Name)
	} else if settings.ShouldIndexInHeader(f.Location[0].Filename) &&
		!commentContains(f.Description, NoDocTag) {
		// TODO(brettw) there can be multiple locations! I think this might be for every
		// forward declaration. In this case we will want to pick the "best" one.
		index.FunctionUsrs[f.USR] = f
		index.FunctionNames[f.Name] = f

		decl := f.Location[0].Filename

		header := index.HeaderForFileName(decl)
		header.Functions = append(header.Functions, f)
		index.Headers[decl] = header
	}
}

func indexRecord(settings IndexSettings, index *Index, r *clangdoc.RecordInfo) {
	if settings.ShouldIndexInHeader(r.DefLocation.Filename) &&
		!commentContains(r.Description, NoDocTag) {
		index.RecordUsrs[r.USR] = r
		index.RecordNames[r.Name] = r

		header := index.HeaderForFileName(r.DefLocation.Filename)
		header.Records = append(header.Records, r)
	}
}

func indexEnum(settings IndexSettings, index *Index, e *clangdoc.EnumInfo) {
	if settings.ShouldIndexInHeader(e.DefLocation.Filename) &&
		!commentContains(e.Description, NoDocTag) {
		index.Enums[e.Name] = e

		header := index.HeaderForFileName(e.DefLocation.Filename)
		header.Enums = append(header.Enums, e)
	}
}

func indexNamespace(settings IndexSettings, index *Index, r *clangdoc.NamespaceInfo) {
	for _, f := range r.ChildFunctions {
		indexFunction(settings, index, f)
	}
	for _, c := range r.ChildNamespaces {
		indexNamespace(settings, index, c)
	}
	for _, r := range r.ChildRecords {
		indexRecord(settings, index, r)
	}
	for _, e := range r.ChildEnums {
		indexEnum(settings, index, e)
	}
}

// Returns true if the two locations have a comment or a blank line separating them.
func (h *Header) hasSeparatorsBetweenLocations(a clangdoc.Location, b clangdoc.Location) bool {
	// Note: line numbers are 1-based.
	if a.LineNumber < 1 || a.LineNumber > len(h.LineClasses) ||
		b.LineNumber < 1 || b.LineNumber > len(h.LineClasses) {
		// Something is out-of-range, assume separated.
		return true
	}
	if a.LineNumber > b.LineNumber {
		log.Fatal("Line numbers not in order")
	}

	for line := a.LineNumber + 1; line < b.LineNumber; line++ {
		// The array is 0-indexed while |line| is 1-indexed.
		if h.LineClasses[line-1] == LineClassBlank || h.LineClasses[line-1] == LineClassComment {
			return true
		}
	}
	return false
}

func (h *Header) groupFunctions(f []*clangdoc.FunctionInfo) []*FunctionGroup {
	if len(f) == 0 {
		return nil
	}

	byLoc := make([]*clangdoc.FunctionInfo, len(f))
	copy(byLoc, f)
	sort.Sort(functionByLocation(byLoc))

	groups := make([]*FunctionGroup, 0, len(f))

	// Makes a new group containing the given function. The group is appended to the list.
	makeNewGroup := func(firstFunc *clangdoc.FunctionInfo) (g *FunctionGroup) {
		g = &FunctionGroup{}
		g.Funcs = make([]*clangdoc.FunctionInfo, 1)
		g.Funcs[0] = firstFunc
		headingLine, _ := extractCommentHeading1(firstFunc.Description)
		g.ExplicitTitle = trimMarkdownHeadings(headingLine)

		groups = append(groups, g)
		return g
	}

	curGroup := makeNewGroup(byLoc[0])

	for i := 1; i < len(byLoc); i++ {
		// Assume if there's no location info there is no separator.
		hasSeparators := len(byLoc[i-1].Location) == 0 || len(byLoc[i].Location) == 0 ||
			h.hasSeparatorsBetweenLocations(byLoc[i-1].Location[0], byLoc[i].Location[0])
		nameMatches := curGroup.Funcs[0].Name == f[i].Name

		if !hasSeparators && (nameMatches || len(curGroup.ExplicitTitle) > 0) {
			// Grouped with previous function
			curGroup.Funcs = append(curGroup.Funcs, byLoc[i])
		} else {
			// Not grouped, this function starts a new one.
			curGroup = makeNewGroup(byLoc[i])
		}
	}

	sort.Sort(functionGroupByTitle(groups))
	return groups
}

func (h *Header) groupDefines(allDefines []*Define) []*DefineGroup {
	if len(allDefines) == 0 {
		return nil
	}

	byLoc := make([]*Define, len(allDefines))
	copy(byLoc, allDefines)
	sort.Sort(defineByLocation(byLoc))

	groups := make([]*DefineGroup, 0, len(allDefines))

	// Makes a new group containing the given define. The group is appended to the list.
	makeNewGroup := func(firstDefine *Define) (g *DefineGroup) {
		g = &DefineGroup{}
		g.Defines = make([]*Define, 1)
		g.Defines[0] = firstDefine
		headingLine, _ := extractCommentHeading1(firstDefine.Description)
		g.ExplicitTitle = trimMarkdownHeadings(headingLine)

		groups = append(groups, g)
		return g
	}

	curGroup := makeNewGroup(byLoc[0])

	for i := 1; i < len(byLoc); i++ {
		// Assume if there's no location info there is no separator.
		hasSeparators := h.hasSeparatorsBetweenLocations(byLoc[i-1].Location, byLoc[i].Location)

		// Unlike functions, there is no name matching logic because defines can't have
		// overloaded names.
		if !hasSeparators && len(curGroup.ExplicitTitle) > 0 {
			// Grouped with previous function
			curGroup.Defines = append(curGroup.Defines, byLoc[i])
		} else {
			// Not grouped, this function starts a new one.
			curGroup = makeNewGroup(byLoc[i])
		}
	}

	sort.Sort(defineGroupByTitle(groups))
	return groups
}

func makeEmptyIndex() Index {
	index := Index{}
	index.FunctionUsrs = make(map[string]*clangdoc.FunctionInfo)
	index.FunctionNames = make(map[string]*clangdoc.FunctionInfo)
	index.RecordNames = make(map[string]*clangdoc.RecordInfo)
	index.RecordUsrs = make(map[string]*clangdoc.RecordInfo)
	index.Headers = make(map[string]*Header)
	index.Defines = make(map[string]*Define)
	index.Enums = make(map[string]*clangdoc.EnumInfo)
	return index
}

func MakeIndex(settings IndexSettings, r *clangdoc.NamespaceInfo) Index {
	index := makeEmptyIndex()
	indexNamespace(settings, &index, r)

	// Get the header comments and #defines for all the headers.
	for name, h := range index.Headers {
		headerValues := ReadHeader(settings.HeaderPath(name))
		h.Description = headerValues.Description
		h.Defines = headerValues.Defines
		h.LineClasses = headerValues.Classes

		// Add the defines to the global index.
		for _, d := range headerValues.Defines {
			index.Defines[d.Name] = d
		}

		// Apply grouping.
		h.FunctionGroups = h.groupFunctions(h.Functions)
		h.DefineGroups = h.groupDefines(h.Defines)
	}

	return index
}
