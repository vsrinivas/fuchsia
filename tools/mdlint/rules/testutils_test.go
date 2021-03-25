// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"bytes"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

type ruleTestCase struct {
	input string
}

type warning struct {
	Ln, Col    int
	TokContent string
}

type warningRecorder struct {
	actual []warning
}

var _ core.Reporter = (*warningRecorder)(nil)

func (r *warningRecorder) Warnf(tok core.Token, format string, a ...interface{}) {
	r.actual = append(r.actual, warning{
		Ln:         tok.Ln,
		Col:        tok.Col,
		TokContent: tok.Content,
	})
}

func (ex ruleTestCase) runOverTokens(t *testing.T, instantiator func(core.Reporter) core.LintRuleOverTokens) {
	var (
		recorder        = &warningRecorder{}
		rule            = instantiator(recorder)
		input, expected = splitMarkdownAndExpectations(ex.input)
	)
	if err := core.ProcessSingleDoc("filename", strings.NewReader(input), rule); err != nil {
		t.Fatalf("error when processing doc: %s", err)
	}
	var (
		numExpected = len(expected)
		numActual   = len(recorder.actual)
		i           int
	)
	if numExpected != numActual {
		t.Errorf("expected %d warning(s), found %d warning(s)", numExpected, numActual)
	}
	for ; i < numExpected && i < numActual; i++ {
		if diff := cmp.Diff(expected[i], recorder.actual[i]); diff != "" {
			t.Errorf("#%d: expected at %d:%d (`%s`), found at %d:%d (`%s`)", i,
				expected[i].Ln, expected[i].Col, expected[i].TokContent,
				recorder.actual[i].Ln, recorder.actual[i].Col, recorder.actual[i].TokContent)
		}
	}
	for ; i < numExpected || i < numActual; i++ {
		if i < numExpected {
			t.Errorf("#%d: expected at %d:%d (`%s`), none found", i,
				expected[i].Ln, expected[i].Col, expected[i].TokContent)
		}
		if i < numActual {
			t.Errorf("#%d: found at %d:%d (`%s`), none expected", i,
				recorder.actual[i].Ln, recorder.actual[i].Col, recorder.actual[i].TokContent)
		}
	}
}

func splitMarkdownAndExpectations(raw string) (string, []warning) {
	var (
		buf, marker bytes.Buffer
		expected    []warning
		ln, col     = 1, 1
		inMarker    bool
	)
	for _, r := range raw {
		switch r {
		case '«':
			if inMarker {
				panic("start of marker («) disallowed within marker")
			}
			inMarker = true
		case '»':
			if !inMarker {
				panic("end of marker (») disallowed outside of marker")
			}
			if marker.Len() == 0 {
				panic("empty marker («») not allowed")
			}
			expected = append(expected, warning{
				Ln:         ln,
				Col:        col,
				TokContent: marker.String(),
			})
			inMarker = false
			marker.Reset()
		case '\n':
			// TODO(fxbug.dev/62964): Add support for markers with newlines.
			if inMarker {
				panic("newline disallowed within marker")
			}
			col = 1
			ln++
			buf.WriteRune(r)
		default:
			if inMarker {
				marker.WriteRune(r)
			} else {
				col++
			}
			buf.WriteRune(r)
		}
	}
	if inMarker {
		panic("non-terminated marker")
	}
	return buf.String(), expected
}
