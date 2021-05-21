// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(newlineBeforeCodeSpanName, newNewlineBeforeCodeSpan)
}

const newlineBeforeCodeSpanName = "newline-before-fenced-code-block"

type newlineBeforeCodeSpan struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter

	newLineCount int
}

var _ core.LintRuleOverTokens = (*newlineBeforeCodeSpan)(nil)

func newNewlineBeforeCodeSpan(reporter core.Reporter) core.LintRuleOverTokens {
	return &newlineBeforeCodeSpan{reporter: reporter}
}

func (rule *newlineBeforeCodeSpan) OnDocStart(_ *core.Doc) {
	rule.newLineCount = 0
}

func (rule *newlineBeforeCodeSpan) OnNext(tok core.Token) {
	if rule.newLineCount < 2 && tok.Kind == core.Code {
		rule.reporter.Warnf(tok, "no emtpy newline before fenced code block")
	}
	if tok.Kind == core.Newline {
		rule.newLineCount = rule.newLineCount + 1
	} else if tok.Kind != core.Space {
		rule.newLineCount = 0
	}
}
