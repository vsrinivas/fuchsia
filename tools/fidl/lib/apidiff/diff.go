// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
			// Note: The calls to backfillForParentStrictness below rely on the
			// fact that elements are processed inside out due to name sorting.
			// For example, if we are currently processing an enum, that means
			// we just finished processing all its members.
			switch {
			// Added a thing.
			case before == nil:
				if after.HasStrictness() {
					// Treat all new declaration as flexible for the purposes of
					// backfilling. For example, when adding a new strict enum,
					// the "addition" of its members is safe even though adding
					// members to existing strict enums is not allowed.
					ret.backfillForParentStrictness(false)
				}
				ret.add(after)
			// Removed a thing.
			case after == nil:
				ret.remove(before)
			// Changed a thing.
			case before != nil && after != nil:
				if after.HasStrictness() {
					// Backfill based on after's strictness. It doesn't really
					// matter if we use before or after because if they differ
					// in strictness, that will be considered API breaking.
					ret.backfillForParentStrictness(after.IsStrict())
				}
				ret.compare(before, after)
			default:
				panic("both before and after are nil")
			}
		})
	if len(ret.backfillIndexes) != 0 {
		panic(fmt.Sprintf("indexes still need backfill: %v", ret.backfillIndexes))
	}
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
