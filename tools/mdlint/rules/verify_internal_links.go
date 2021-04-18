// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverPatterns(verifyInternalLinksName, newVerifyInternalLinks)
}

const verifyInternalLinksName = "verify-internal-links"

type verifyInternalLinks struct {
	core.DefaultLintRuleOverPatterns
	reporter core.Reporter

	byXref []core.Token
	xrefs  map[string]string
}

var _ core.LintRuleOverPatterns = (*verifyInternalLinks)(nil)

func newVerifyInternalLinks(reporter core.Reporter) core.LintRuleOverPatterns {
	return &verifyInternalLinks{reporter: reporter}
}

func (rule *verifyInternalLinks) OnDocStart(doc *core.Doc) {
	rule.byXref = nil
	rule.xrefs = make(map[string]string)
}

func (rule *verifyInternalLinks) OnLinkByXref(xref core.Token) {
	rule.byXref = append(rule.byXref, xref)
}

func (rule *verifyInternalLinks) OnXrefDefinition(xref, url core.Token) {
	// TODO(fxbug.dev/62964): internal or external? if external, need to check presence
	normalized := normalizeLinkLabel(xref.Content)
	if _, ok := rule.xrefs[normalized]; ok {
		rule.reporter.Warnf(xref, "internal reference defined multiple times")
	} else {
		rule.xrefs[normalized] = url.Content
	}
}

func (rule *verifyInternalLinks) OnDocEnd() {
	for _, xref := range rule.byXref {
		if _, ok := rule.xrefs[normalizeLinkLabel(xref.Content)]; !ok {
			rule.reporter.Warnf(xref, "internal reference not defined")
		}
	}
	rule.byXref = nil
	rule.xrefs = nil
}
