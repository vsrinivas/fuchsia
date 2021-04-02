// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package apidiff contains the code used for computing FIDL API
// differences.
package apidiff

import (
	"fmt"
	"sort"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

// Compute computes the API difference between before and after.
func Compute(before, after []summarize.ElementStr) (Report, error) {
	if err := isSorted(before); err != nil {
		return Report{}, fmt.Errorf("while processing 'before': %w", err)
	}
	if err := isSorted(after); err != nil {
		return Report{}, fmt.Errorf("while processing 'after': %w", err)
	}
	var ret Report
	parallelIter(before, after,
		func(before, after *summarize.ElementStr) {
			switch {
			// Added a thing.
			case before == nil:
				// If it has strictness, then check if the elements need to be
				// backfilled.
				if after.HasStrictness() {
					// If a declaration with strictness is added, we know that
					// any added elements are unused, so they don't break the
					// API.
					ret.BackfillForParentStrictness(false)
				}
				ret.add(after)
			// Removed a thing.
			case after == nil:
				ret.remove(before)
			// Both exist.
			case before != nil && after != nil:
				if after.HasStrictness() {
					// A declaration with strictness already exists.
					ret.BackfillForParentStrictness(after.IsStrict())
				}
				ret.compare(before, after)
			default:
				panic("both before and after are nil - that is a programming error")
			}
		})
	return ret, nil
}

// parallelIter iterates in parallel over the two slices, before and
// after, and invokes forEachFn for each pair of elements iterated over.
// The elements are matched by summarize.ElementStr.Less, and if two
// elements don't match, then the lesser element is processed first.
func parallelIter(before, after []summarize.ElementStr, forEachFn func(before, after *summarize.ElementStr)) {
	a, b := 0, 0
	for b < len(before) || a < len(after) {
		var curBefore, curAfter *summarize.ElementStr
		switch {
		case b >= len(before):
			// before has been consumed, only after remains.
			curAfter = &after[a]
			a++
		case a >= len(after):
			curBefore = &before[b]
			b++
		case b < len(before) && a < len(after):
			// Both are still available.  Iterate items equal by name
			// together, otherwise lesser item first.
			r := cmpFn(before[b], after[a])

			// Both 'if' branches below are taken when r==0, and that is on purpose.
			if r <= 0 { // Less or equal.
				curBefore = &before[b]
				b++
			}
			if r >= 0 { // Greater or equal.
				curAfter = &after[a]
				a++
			}
		default:
			// Ideally wouldn't be needed, but this helps in case the Less
			// operation on ElementStr has a bug.
			panic(fmt.Sprintf(
				"neither before nor after are candidates: b=%v, len(before)=%v, a=%v, len(after)=%v",
				b, len(before), a, len(after)))
		}
		forEachFn(curBefore, curAfter)
	}
}

// cmpFn is a three-way comparison between two ElementStrs.
// Returns -1 if a is less, 0 if equal, and 1 if b is less.
func cmpFn(a, b summarize.ElementStr) int {
	l1 := a.Less(b)
	l2 := b.Less(a)
	switch {
	case l1:
		return -1
	case l2:
		return 1
	case !l1 && !l2:
		return 0
	default:
		// Defensive coding in case ElementStr.Less has a bug.
		panic(fmt.Sprintf("unexpected cmp: l1=%+v, l2=%+v", a, b))
	}
}

// isSorted returns an error if the slice is not sorted.
func isSorted(slice []summarize.ElementStr) error {
	if !sort.SliceIsSorted(slice, func(i, j int) bool {
		return slice[i].Less(slice[j])
	}) {
		return fmt.Errorf("slice is not sorted but should be")
	}
	return nil
}
