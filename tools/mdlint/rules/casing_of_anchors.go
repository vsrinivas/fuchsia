// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"regexp"

	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(casingOfAnchorsName, func(reporter core.Reporter) core.LintRuleOverTokens {
		return &casingOfAnchors{reporter: reporter}
	})
}

const casingOfAnchorsName = "casing-of-anchors"

type casingOfAnchors struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter
}

var _ core.LintRuleOverTokens = (*casingOfAnchors)(nil)

var casingOfAnchorsRe = regexp.MustCompile("{#[a-zA-Z0-9]+([a-zA-Z0-9-]*[a-zA-Z0-9])?}")

func (rule *casingOfAnchors) OnNext(tok core.Token) {
	if tok.Kind == core.Anchor {
		if !casingOfAnchorsRe.MatchString(tok.Content) {
			rule.reporter.Warnf(tok, "poorly formated anchor")
		}
	}
}
