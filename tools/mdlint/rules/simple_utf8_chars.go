// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(simpleUtf8CharsName, newSimpleUtf8Chars)
}

const simpleUtf8CharsName = "simple-utf8-chars"

type simpleUtf8Chars struct {
	core.DefaultLintRuleOverTokens
	core.Reporter
}

var _ core.LintRuleOverTokens = (*simpleUtf8Chars)(nil)

func newSimpleUtf8Chars(reporter core.Reporter) core.LintRuleOverTokens {
	return &simpleUtf8Chars{Reporter: reporter}
}

var dontButPrefer = map[rune]string{
	'“': `"`,
	'”': `"`,
	'‘': `'`,
	'’': `'`,
	'…': `...`,
}

func (rule *simpleUtf8Chars) OnNext(tok core.Token) {
	for _, r := range tok.Content {
		if sub, ok := dontButPrefer[r]; ok {
			rule.Warnf(tok, "avoid %c, prefer %s", r, sub)
			return // avoid double reporting on same token
		}
	}
}
