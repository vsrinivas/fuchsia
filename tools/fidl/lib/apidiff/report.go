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

// Classification describes the compatibility of a change.
type Classification string

const (
	// NeedsBackfill means the change could not be classified on its own, but
	// requires another pass where its enclosing declaration is known. See
	// backfillForParentStrictness().
	NeedsBackfill Classification = "NeedsBackfill"
	// APIBreaking change will break compilation for clients.
	APIBreaking Classification = "APIBreaking"
	// Transitionable change can be made as a sequence of SourceCompatible
	// changes.
	Transitionable Classification = "Transitionable"
	// SourceCompatible change does not break compilation.
	SourceCompatible Classification = "SourceCompatible"
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
	// APIDiff has the report items for each individual change of the API
	// surface for a FIDL library.
	APIDiff []ReportItem `json:"api_diff,omitempty"`

	// backfillIndexes contains APIDiff indexes which have a NeedsBackfill
	// classification, to be handled by backfillForParentStrictness().
	backfillIndexes []int
}

// backfillForParentStrictness backfills all APIDiff indexes based on the given
// strictness. This is called when processing a declaration, after having
// already processed its members (and put them in r.backfillIndexes).
func (r *Report) backfillForParentStrictness(isStrict bool) {
	for _, i := range r.backfillIndexes {
		if isStrict {
			r.APIDiff[i].Conclusion = APIBreaking
		} else {
			r.APIDiff[i].Conclusion = SourceCompatible
		}
	}
	r.backfillIndexes = nil
}

func (r *Report) addToDiff(rep ReportItem) {
	switch rep.Conclusion {
	case "":
		panic(fmt.Sprintf("unset conclusion: %+v", rep))
	case NeedsBackfill:
		r.backfillIndexes = append(r.backfillIndexes, len(r.APIDiff))
	}
	r.APIDiff = append(r.APIDiff, rep)
}

// WriteJSON writes a report as JSON.
func (r Report) WriteJSON(w io.Writer) error {
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
	switch item.Kind {
	case "bits", "enum", "struct", "library", "const",
		"table", "union", "protocol", "alias",
		"table/member", "bits/member":
		ret.Conclusion = SourceCompatible
	case "enum/member", "union/member":
		ret.Conclusion = NeedsBackfill
	case "struct/member":
		ret.Conclusion = APIBreaking
	case "protocol/member":
		ret.Conclusion = Transitionable
	default:
		panic(fmt.Sprintf("unexpected item kind: %+v", item))
	}
	r.addToDiff(ret)
}

// remove processes a single removed ElementStr.
func (r *Report) remove(item *summarize.ElementStr) {
	ret := ReportItem{
		Name:       item.Name,
		Before:     item,
		Conclusion: APIBreaking,
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
	switch {
	case before.Name != after.Name:
		panic(fmt.Sprintf("before name %q != after name %q", before.Name, after.Name))
	case before.Kind != after.Kind,
		before.Decl != after.Decl,
		before.Value != after.Value,
		before.Resourceness != after.Resourceness:
		ret.Conclusion = APIBreaking
	case before.Strictness != after.Strictness:
		ret.Conclusion = Transitionable
	default:
		panic(fmt.Sprintf("unexpected difference: before = %+v, after = %+v", before, after))
	}
	r.addToDiff(ret)
}
