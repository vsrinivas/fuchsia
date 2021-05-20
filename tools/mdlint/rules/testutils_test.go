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
	files map[string]string
}

type warning struct {
	Ln, Col    int
	TokContent string
}

type warningRecorder struct {
	actual []warning

	// toks is a parallel array to actual recording the tokens which were warned
	// on.
	toks []core.Token
}

var _ core.Reporter = (*warningRecorder)(nil)

func (r *warningRecorder) Warnf(tok core.Token, format string, a ...interface{}) {
	r.actual = append(r.actual, warning{
		Ln:         tok.Ln,
		Col:        tok.Col,
		TokContent: tok.Content,
	})
	r.toks = append(r.toks, tok)
}

func (ex ruleTestCase) runOverTokens(t *testing.T, instantiator func(core.Reporter) core.LintRuleOverTokens) {
	t.Helper()

	var (
		recorder    = &warningRecorder{}
		rule        = instantiator(recorder)
		allExpected = make(map[string][]warning)
		allActual   = make(map[string][]warning)
	)

	// 1. Process all files, purposefully in any order.
	rule.OnStart()
	for filename, filecontent := range ex.files {
		input, expected := splitMarkdownAndExpectations(filecontent)
		if err := core.ProcessSingleDoc(filename, strings.NewReader(input), rule); err != nil {
			t.Fatalf("error when processing doc: %s", err)
		}
		allExpected[filename] = expected
	}
	rule.OnEnd()

	// 2. Organize all actual by filename.
	for i, singleActual := range recorder.actual {
		filename := recorder.toks[i].Doc.Filename
		allActual[filename] = append(allActual[filename], singleActual)
	}

	// 3. Verify all expected against all actual.
	for filename := range ex.files {
		var (
			expected = allExpected[filename]
			actual   = allActual[filename]

			numExpected = len(expected)
			numActual   = len(actual)
			i           int
		)
		if numExpected != numActual {
			t.Errorf("expected %d warning(s), found %d warning(s)", numExpected, numActual)
		}
		for ; i < numExpected && i < numActual; i++ {
			if diff := cmp.Diff(expected[i], actual[i]); diff != "" {
				t.Errorf("#%d: expected at %d:%d (`%q`), found at %d:%d (`%q`)", i,
					expected[i].Ln, expected[i].Col, expected[i].TokContent,
					actual[i].Ln, actual[i].Col, actual[i].TokContent)
			}
		}
		for ; i < numExpected || i < numActual; i++ {
			if i < numExpected {
				t.Errorf("#%d: expected at %d:%d (`%q`), none found", i,
					expected[i].Ln, expected[i].Col, expected[i].TokContent)
			}
			if i < numActual {
				t.Errorf("#%d: found at %d:%d (`%q`), none expected", i,
					actual[i].Ln, actual[i].Col, actual[i].TokContent)
			}
		}
	}
}

func (ex ruleTestCase) runOverPatterns(t *testing.T, instantiator func(core.Reporter) core.LintRuleOverPatterns) {
	ex.runOverTokens(t, func(reporter core.Reporter) core.LintRuleOverTokens {
		return core.CombineRules(nil, []core.LintRuleOverPatterns{instantiator(reporter)})
	})
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
			for _, mr := range marker.String() {
				switch mr {
				case '\n':
					col = 1
					ln++
				default:
					col++
				}
			}
			inMarker = false
			marker.Reset()
		case '\n':
			if inMarker {
				marker.WriteRune(r)
			} else {
				col = 1
				ln++
			}
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
