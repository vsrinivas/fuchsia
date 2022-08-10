// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"sort"
)

type IndexSettings struct {
	// Names of the header files we want to index.
	Headers map[string]struct{}
}

func (s IndexSettings) ShouldIndexInHeader(hdrName string) bool {
	_, found := s.Headers[hdrName]
	return found
}

// Flattened version of everything stored in this library.
type Index struct {
	// All nonmember functions/records in all non-anonymous namespaces, indexed by the USR.
	Functions map[string]*clangdoc.FunctionInfo

	// Records indexed by the USR.
	Records map[string]*clangdoc.RecordInfo

	// All unique header files in this library indexed by their file name.
	Headers map[string]*Header
}

// Returns a sorted list of all functions.
func (index *Index) AllFunctions() []*clangdoc.FunctionInfo {
	result := make([]*clangdoc.FunctionInfo, 0, len(index.Functions))
	for _, fn := range index.Functions {
		result = append(result, fn)
	}
	sort.Sort(functionByName(result))
	return result
}

// Returns a sorted list of all records.
func (index *Index) AllRecords() []*clangdoc.RecordInfo {
	result := make([]*clangdoc.RecordInfo, 0, len(index.Records))
	for _, r := range index.Records {
		result = append(result, r)
	}
	sort.Sort(recordByName(result))
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

func indexFunction(settings IndexSettings, index *Index, f *clangdoc.FunctionInfo) {
	if len(f.Location) == 0 {
		fmt.Printf("WARNING: Function %s does not have a declaration location.\n", f.Name)
	} else if settings.ShouldIndexInHeader(f.Location[0].Filename) {
		// TODO(brettw) there can be multiple locations! I think this might be for every
		// forward declaration. In this case we will want to pick the "best" one.
		index.Functions[f.USR] = f

		decl := f.Location[0].Filename

		header := index.HeaderForFileName(decl)
		header.Functions = append(header.Functions, f)
		index.Headers[decl] = header
	}
}

func indexRecord(settings IndexSettings, index *Index, r *clangdoc.RecordInfo) {
	if settings.ShouldIndexInHeader(r.DefLocation.Filename) {
		index.Records[r.USR] = r

		header := index.HeaderForFileName(r.DefLocation.Filename)
		header.Records = append(header.Records, r)
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

	// TODO enums
}

func makeEmptyIndex() Index {
	index := Index{}
	index.Functions = make(map[string]*clangdoc.FunctionInfo)
	index.Records = make(map[string]*clangdoc.RecordInfo)
	index.Headers = make(map[string]*Header)
	return index
}

func MakeIndex(settings IndexSettings, r *clangdoc.NamespaceInfo) Index {
	index := makeEmptyIndex()
	indexNamespace(settings, &index, r)

	// TODO sort records

	return index
}
