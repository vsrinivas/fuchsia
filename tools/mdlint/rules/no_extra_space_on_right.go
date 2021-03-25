// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(noExtraSpaceOnRightName, newNoExtraSpaceOnRight)
}

const noExtraSpaceOnRightName = "no-extra-space-on-right"

type noExtraSpaceOnRight struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter

	lastTok core.Token
}

var _ core.LintRuleOverTokens = (*noExtraSpaceOnRight)(nil)

func newNoExtraSpaceOnRight(reporter core.Reporter) core.LintRuleOverTokens {
	return &noExtraSpaceOnRight{reporter: reporter}
}

func (rule *noExtraSpaceOnRight) OnNext(tok core.Token) {
	if rule.lastTok.Kind == core.Space && tok.Kind == core.Newline {
		rule.reporter.Warnf(rule.lastTok, "extra whitespace")
	}
	rule.lastTok = tok
}
