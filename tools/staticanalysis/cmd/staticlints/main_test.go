// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

type fakeAnalyzer struct{}

var _ staticanalysis.Analyzer = fakeAnalyzer{}

func (c fakeAnalyzer) Analyze(_ context.Context, path string) ([]*staticanalysis.Finding, error) {
	return []*staticanalysis.Finding{
		{
			Message:   "variable not defined",
			Category:  "FakeTool/name_error",
			Path:      path,
			StartLine: 1,
			StartChar: 2,
			EndChar:   2,
		},
	}, nil
}

func TestRunAnalyzers(t *testing.T) {
	analyzers := []staticanalysis.Analyzer{fakeAnalyzer{}}
	findings, err := runAnalyzers(context.Background(), analyzers, []string{"src/foo.py", "src/bar.py"})
	if err != nil {
		t.Fatal(err)
	}
	expected := []*staticanalysis.Finding{
		{
			Message:   "variable not defined",
			Category:  "FakeTool/name_error",
			Path:      "src/foo.py",
			StartLine: 1,
			EndLine:   1,
			StartChar: 2,
			EndChar:   3,
		},
		{
			Message:   "variable not defined",
			Category:  "FakeTool/name_error",
			Path:      "src/bar.py",
			StartLine: 1,
			EndLine:   1,
			StartChar: 2,
			EndChar:   3,
		},
	}

	if diff := cmp.Diff(expected, findings); diff != "" {
		t.Errorf("runAnalyzers() diff (-want +got): %s", diff)
	}
}
