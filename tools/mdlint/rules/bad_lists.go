// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(badListsName, newBadLists)
}

const badListsName = "bad-lists"

type badLists struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter

	buf    [2]core.Token
	i      int
	inList bool
}

var _ core.LintRuleOverTokens = (*badLists)(nil)

func newBadLists(reporter core.Reporter) core.LintRuleOverTokens {
	return &badLists{reporter: reporter}
}

func (rule *badLists) OnDocStart(_ *core.Doc) {
	var fakeNewlineTok core.Token
	fakeNewlineTok.Kind = core.Newline
	rule.buf[0] = fakeNewlineTok
	rule.buf[1] = fakeNewlineTok
	rule.inList = false
}

func (rule *badLists) OnNext(tok core.Token) {
	switch tok.Kind {
	case core.Space:
		return // ignore
	case core.List:
		if !rule.inList && !(rule.buf[0].Kind == core.Newline && rule.buf[1].Kind == core.Newline) {
			rule.reporter.Warnf(tok, "must have extra new line before a list")
			var defaultTok core.Token
			rule.buf[0] = defaultTok
			rule.buf[1] = defaultTok
		}
		rule.inList = true
	case core.Newline:
		if rule.inList && rule.buf[(rule.i+1)%2].Kind == core.Newline {
			rule.inList = false
		}
	}
	rule.buf[rule.i] = tok
	rule.i = (rule.i + 1) % 2
}
