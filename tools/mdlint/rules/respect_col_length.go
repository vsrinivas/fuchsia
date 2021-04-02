// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
	"strings"
)

func init() {
	core.RegisterLintRuleOverTokens(respectColLengthName, newRespectColLength)
}

const respectColLengthName = "respect-col-length"

type respectColLength struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter

	countStartingAtCol int
	lastReportedLine   int
}

var _ core.LintRuleOverTokens = (*respectColLength)(nil)

func newRespectColLength(reporter core.Reporter) core.LintRuleOverTokens {
	return &respectColLength{reporter: reporter}
}

// colSizeLimit controls the maximum number of characters on a single line.
const colSizeLimit = 80

func (rule *respectColLength) OnDocStart(_ *core.Doc) {
	rule.countStartingAtCol = 1
	rule.lastReportedLine = 0
}

func (rule *respectColLength) OnNext(tok core.Token) {
	switch tok.Kind {
	case core.Space:
		// Do nothing.
	case core.Newline:
		rule.countStartingAtCol = 1
	case core.Header, core.List:
		rule.countStartingAtCol = len(tok.Content) + 1
	default:
		if tok.Ln == rule.lastReportedLine || tok.Col == 1 {
			break
		}
		var length int
		if i := strings.IndexRune(tok.Content, '\n'); i >= 0 {
			length = i
		} else {
			length = len(tok.Content)
		}
		if endCol := tok.Col + length - rule.countStartingAtCol; endCol > colSizeLimit {
			rule.reporter.Warnf(tok, "line over %d column limit", colSizeLimit)
			rule.lastReportedLine = tok.Ln
		}
	}
}
