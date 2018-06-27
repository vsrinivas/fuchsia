// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"sort"
)

// TODO (jakehehrlich): This whole file should be private.

type byVAddr []Segment

func (b byVAddr) Len() int {
	return len(b)
}

func (b byVAddr) Swap(i, j int) {
	b[i], b[j] = b[j], b[i]
}

func (b byVAddr) Less(i, j int) bool {
	return b[i].Vaddr < b[j].Vaddr
}

// TODO: replace with skip list
// I call it a "lazy flat map". It stores mapped regions of memory in a way that
// allows them to be efficentily looked up by an address within them.

// MappingStore quasi-efficently indexes segments by their start address.
type mappingStore struct {
	segments []Segment
	sorted   int
}

func merge(a []Segment, b []Segment) []Segment {
	out := []Segment{}
	for len(a) != 0 && len(b) != 0 {
		var min Segment
		if a[0].Vaddr < b[0].Vaddr {
			min = a[0]
			a = a[1:]
		} else {
			min = b[0]
			b = b[1:]
		}
		out = append(out, min)
	}
	out = append(out, a...)
	out = append(out, b...)
	return out
}

// sortAndFind is meant to be called if an element couldn't be found.
// sortAndFind sorts the unsorted range of segments, finds the missing element
// and then merges the two sorted ranges so that sortAndFind won't have to be
// called again for the same element.
func (m *mappingStore) sortAndFind(vaddr uint64) *Segment {
	sort.Sort(byVAddr(m.segments[m.sorted:]))
	seg := findSegment(m.segments[m.sorted:], vaddr)
	newMods := merge(m.segments[m.sorted:], m.segments[:m.sorted])
	m.segments = newMods
	m.sorted = len(newMods)
	return seg
}

// findSegment finds a segment in sorted slice of segments.
func findSegment(sorted []Segment, vaddr uint64) *Segment {
	idx := sort.Search(len(sorted), func(i int) bool {
		seg := sorted[i]
		return seg.Vaddr+seg.Size >= vaddr
	})
	if idx < len(sorted) && sorted[idx].Vaddr <= vaddr {
		return &sorted[idx]
	}
	return nil
}

// Find first trys to find the desired segment in the sorted segment. If the segment
// can't be found we consult the unsorted part and update the structure.
func (m *mappingStore) find(vaddr uint64) *Segment {
	out := findSegment(m.segments[:m.sorted], vaddr)
	if out == nil {
		out = m.sortAndFind(vaddr)
	}
	return out
}

// Add adds a segment to the segment.
func (m *mappingStore) add(seg Segment) {
	m.segments = append(m.segments, seg)
}

// Clear clears the mapping store of all previous information
func (m *mappingStore) clear() {
	m.segments = nil
	m.sorted = 0
}

// GetSegments returns a new slice containing all the segments
func (m *mappingStore) GetSegments() []Segment {
	out := make([]Segment, len(m.segments))
	copy(out, m.segments)
	return out
}
