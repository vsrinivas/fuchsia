// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

type commonLintRule interface {
	onStart()
	onDocStart(doc *doc)
	onDocEnd()
	onEnd()
}

type lintRuleOverTokens interface {
	commonLintRule

	onNext(tok token)
}

// The defaultLintRuleOverTokens provides default implementation for the
// lintRuleOverTokens interface.
type defaultLintRuleOverTokens struct{}

var _ lintRuleOverTokens = (*defaultLintRuleOverTokens)(nil)

func (defaultLintRuleOverTokens) onStart()            {}
func (defaultLintRuleOverTokens) onDocStart(doc *doc) {}
func (defaultLintRuleOverTokens) onDocEnd()           {}
func (defaultLintRuleOverTokens) onEnd()              {}

func (defaultLintRuleOverTokens) onNext(tok token) {}

type lintRuleOverEvents interface {
	commonLintRule

	onLinkByXref(xref token)
	onLinkByURL(url token)
	onXrefDefinition(xref, url token)
	onTableOfContents(toc token)
}

// The defaultLintRuleOverEvents provides default implementation for the
// lintRuleOverEvents interface.
type defaultLintRuleOverEvents struct{}

var _ lintRuleOverEvents = (*defaultLintRuleOverEvents)(nil)

func (defaultLintRuleOverEvents) onStart()            {}
func (defaultLintRuleOverEvents) onDocStart(doc *doc) {}
func (defaultLintRuleOverEvents) onDocEnd()           {}
func (defaultLintRuleOverEvents) onEnd()              {}

func (defaultLintRuleOverEvents) onLinkByXref(xref token)          {}
func (defaultLintRuleOverEvents) onLinkByURL(url token)            {}
func (defaultLintRuleOverEvents) onXrefDefinition(xref, url token) {}
func (defaultLintRuleOverEvents) onTableOfContents(toc token)      {}

// oneToManyOverTokens is a lintRuleOverTokens which simply dispatches to many
// lintRuleOverTokens, therefore allowing to combine multiple rules where only
// one can be plugged in.
type oneToManyOverTokens []lintRuleOverTokens

var _ lintRuleOverTokens = (*oneToManyOverTokens)(nil)

func (rules oneToManyOverTokens) onStart() {
	for _, rule := range rules {
		rule.onStart()
	}
}

func (rules oneToManyOverTokens) onDocStart(doc *doc) {
	for _, rule := range rules {
		rule.onDocStart(doc)
	}
}

func (rules oneToManyOverTokens) onDocEnd() {
	for _, rule := range rules {
		rule.onDocEnd()
	}
}

func (rules oneToManyOverTokens) onEnd() {
	for _, rule := range rules {
		rule.onEnd()
	}
}

func (rules oneToManyOverTokens) onNext(tok token) {
	for _, rule := range rules {
		rule.onNext(tok)
	}
}

// oneToManyOverEvents is a lintRuleOverEvents which simply dispatches to many
// lintRuleOverEvents, therefore allowing to combine multiple rules where only
// one can be plugged in.
type oneToManyOverEvents []lintRuleOverEvents

var _ lintRuleOverEvents = (*oneToManyOverEvents)(nil)

func (rules oneToManyOverEvents) onStart() {
	for _, rule := range rules {
		rule.onStart()
	}
}

func (rules oneToManyOverEvents) onDocStart(doc *doc) {
	for _, rule := range rules {
		rule.onDocStart(doc)
	}
}

func (rules oneToManyOverEvents) onDocEnd() {
	for _, rule := range rules {
		rule.onDocEnd()
	}
}

func (rules oneToManyOverEvents) onEnd() {
	for _, rule := range rules {
		rule.onEnd()
	}
}

func (rules oneToManyOverEvents) onLinkByXref(xref token) {
	for _, rule := range rules {
		rule.onLinkByXref(xref)
	}
}

func (rules oneToManyOverEvents) onLinkByURL(url token) {
	for _, rule := range rules {
		rule.onLinkByURL(url)
	}
}

func (rules oneToManyOverEvents) onXrefDefinition(xref, url token) {
	for _, rule := range rules {
		rule.onXrefDefinition(xref, url)
	}
}

func (rules oneToManyOverEvents) onTableOfContents(toc token) {
	for _, rule := range rules {
		rule.onTableOfContents(toc)
	}
}
