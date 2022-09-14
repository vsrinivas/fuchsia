// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package apidiff

import (
	"encoding/json"
	"fmt"
	"io"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

// ReportItem is a single line item of the API diff report.
type ReportItem struct {
	// Name is the fully qualified name that this report item
	// pertains to.
	Name summarize.Name `json:"name"`
	// Before is what the API summary used to look like.
	Before *summarize.ElementStr `json:"before,omitempty"`
	// After is what the API summary looks like now.
	After *summarize.ElementStr `json:"after,omitempty"`
	// Conclusion is the finding.
	Conclusion Classification `json:"conclusion"`
}

func (r ReportItem) IsAdd() bool {
	return r.Before == nil && r.After != nil
}

func (r ReportItem) IsRemove() bool {
	return r.Before != nil && r.After == nil
}

func (r ReportItem) IsChange() bool {
	return r.Before != nil && r.After != nil
}

// Report is a top-level wrapper for the API diff result.
type Report struct {
	// ApiDiff has the report items for each individual change of the API
	// surface for a FIDL library.
	ApiDiff []ReportItem `json:"api_diff,omitempty"`

	// backfillIndexes is a list of indexes into ApiDiff which have a
	// classification of Undetermined.
	//
	// These reportItems could not have been classified at the point that they
	// were seen in the summary because there was not enough information to do
	// so.  Instead, we remember their indexes here, and when we eventually
	// process the parent declaration we get enough info to classify, and only
	// then go back to them to finish the classification.
	//
	// A report that is "finalized" (ready to write out) *must* have this value
	// set to `nil`.  This is done by caling `BackfillForParentStrictness` at
	// some point while processing the report.
	backfillIndexes []int
}

// BackfillForParentStrictness backfills all ApiDiff indexes based on the
// appropriate strictness.
func (r *Report) BackfillForParentStrictness(isStrict bool) {
	for _, i := range r.backfillIndexes {
		// Get a pointer to the element so it can be mutated in place.
		elem := &r.ApiDiff[i]
		if elem.Conclusion != Undetermined {
			panic(fmt.Sprintf(
				"BackfillForParentStrictness: found a determined report in a list of backfill indexes: report: %+v",
				*r))
		}
		if isStrict {
			elem.Conclusion = APIBreaking
		} else {
			elem.Conclusion = SourceCompatible
		}
	}
	r.backfillIndexes = nil
}

func (r *Report) addToDiff(rep ReportItem) {
	if rep.Conclusion == 0 {
		panic(fmt.Sprintf("Unset conclusion: %+v", rep))
	}
	r.ApiDiff = append(r.ApiDiff, rep)
}

// WriteJSON writes a report as JSON.
func (r Report) WriteJSON(w io.Writer) error {
	if len(r.backfillIndexes) != 0 {
		panic(fmt.Sprintf("Report.WriteJSON: programming error, backfillIndexes = %v", r.backfillIndexes))
	}
	e := json.NewEncoder(w)
	e.SetEscapeHTML(false)
	e.SetIndent("", "  ")
	if err := e.Encode(r); err != nil {
		return fmt.Errorf("while writing JSON: %w", err)
	}
	return nil
}

// add processes a single added ElementStr.
func (r *Report) add(item *summarize.ElementStr) {
	ret := ReportItem{
		Name:  item.Name,
		After: item,
	}
	// TODO: compress this table if possible after all diffs have been
	// accounted for.
	switch item.Kind {
	case "bits", "enum", "struct", "library", "const",
		"table", "union", "protocol", "alias":
		ret.Conclusion = SourceCompatible
	case "enum/member", "union/member":
		// The conclusion here depends on whether the enclosing declaration is
		// strict or flexible, and whether that enclosing declaration is used.
		// Defer the conclusion until we get to processing the enclosing
		// declaration.
		ret.Conclusion = Undetermined
		r.backfillIndexes = append(r.backfillIndexes, len(r.ApiDiff))
	case "struct/member":
		// Breaks Rust initialization.
		ret.Conclusion = APIBreaking
	case "table/member", "bits/member":
		ret.Conclusion = SourceCompatible
	case "protocol/member":
		ret.Conclusion = Transitionable
	default:
		panic(fmt.Sprintf("Report.add: unknown kind: %+v", item))
	}
	r.addToDiff(ret)
}

// remove processes a single removed ElementStr
func (r *Report) remove(item *summarize.ElementStr) {
	ret := ReportItem{
		Name:   item.Name,
		Before: item,
	}
	// TODO: compress this table if possible after all diffs have been
	// accounted for.
	switch item.Kind {
	case "library", "const", "bits", "enum", "struct",
		"table", "union", "protocol", "alias",
		"struct/member", "table/member", "bits/member",
		"enum/member", "union/member", "protocol/member":
		ret.Conclusion = APIBreaking
	default:
		panic(fmt.Sprintf("Report.remove: unknown kind: %+v", item))
	}
	r.addToDiff(ret)
}

func (r *Report) compare(before, after *summarize.ElementStr) {
	if *before == *after {
		// No change
		return
	}
	ret := ReportItem{
		Name:   after.Name,
		Before: before,
		After:  after,
	}
	// 'defer r.addToDiff(ret)` wouldn't work, because it would bind the *current*
	// value of ret to the function call, and we want the final value to be
	// used.
	defer func() {
		r.addToDiff(ret)
	}()
	if before.Kind != after.Kind {
		ret.Conclusion = APIBreaking
		return
	}
	switch after.Kind {
	case "const", "struct/member", "table/member", "union/member", "bits/member", "enum/member":
		// While the code today does not distinguish between specific types of API breaks,
		// the ones currently possible are:
		// 1. type change (possible for all)
		// 2. value change (not possible for table/member, and union/member).
		ret.Conclusion = APIBreaking
	case "bits", "enum":
		switch {
		// Underlying type change.
		case before.Decl != after.Decl:
			ret.Conclusion = APIBreaking
		case before.IsStrict() != after.IsStrict():
			ret.Conclusion = Transitionable
		case before != after: // Type change
			ret.Conclusion = APIBreaking
		}
	case "struct", "table", "union":
		switch {
		case before.Resourceness != after.Resourceness:
			ret.Conclusion = APIBreaking
		default:
			ret.Conclusion = Transitionable
		}
	case "protocol/member":
		ret.Conclusion = APIBreaking
	case "protocol":
		fallthrough
	default:
		panic(fmt.Sprintf(
			"Report.compare: kind not handled: %v; this is a programer error",
			after.Kind))
	}
}
