// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(noExtraSpaceAtStartOfDocName, newNoExtraSpaceAtStartOfDoc)
}

const noExtraSpaceAtStartOfDocName = "no-extra-space-at-start-of-doc"

type noExtraSpaceAtStartOfDoc struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter

	seenContent       bool
	warnOnNextContent bool
}

var _ core.LintRuleOverTokens = (*noExtraSpaceAtStartOfDoc)(nil)

func newNoExtraSpaceAtStartOfDoc(reporter core.Reporter) core.LintRuleOverTokens {
	return &noExtraSpaceAtStartOfDoc{reporter: reporter}
}

func (rule *noExtraSpaceAtStartOfDoc) OnDocStart(_ *core.Doc) {
	rule.seenContent = false
	rule.warnOnNextContent = false
}

func (rule *noExtraSpaceAtStartOfDoc) OnNext(tok core.Token) {
	if !rule.seenContent {
		if tok.Kind == core.Newline || tok.Kind == core.Space {
			rule.warnOnNextContent = true
		} else {
			rule.seenContent = true
		}
	}

	if rule.warnOnNextContent && (tok.Kind != core.Space && tok.Kind != core.Newline) {
		rule.reporter.Warnf(tok, "no extra space should appear before the start of content")
		rule.warnOnNextContent = false
	}
}
