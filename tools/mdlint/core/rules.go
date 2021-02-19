// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"io"
)

// ProcessSingleDoc processes a single document.
func ProcessSingleDoc(filename string, stream io.Reader, rule LintRuleOverTokens) error {
	doc := newDoc(filename, stream)
	rule.OnDocStart(doc)
	defer rule.OnDocEnd()

	tokenizer := newTokenizer(doc)
	for {
		tok, err := tokenizer.next()
		if err != nil {
			return err
		}
		rule.OnNext(tok)
		if tok.Kind == EOF {
			return nil
		}
	}
}

type commonLintRule interface {
	// OnStart is called when the linter starts.
	OnStart()

	// OnDocStart is called when a document starts to being read.
	OnDocStart(doc *Doc)

	// OnDocStart is called when a document has been fully read.
	OnDocEnd()

	// OnEnd is called when the linter has completed.
	OnEnd()
}

// LintRuleOverTokens defines how rules over tokens operate.
type LintRuleOverTokens interface {
	commonLintRule

	// OnNext is called for each token processed as part of reading a document.
	OnNext(tok Token)
}

// The DefaultLintRuleOverTokens provides default implementation for the
// LintRuleOverTokens interface.
type DefaultLintRuleOverTokens struct{}

var _ LintRuleOverTokens = (*DefaultLintRuleOverTokens)(nil)

func (DefaultLintRuleOverTokens) OnStart()            {}
func (DefaultLintRuleOverTokens) OnDocStart(doc *Doc) {}
func (DefaultLintRuleOverTokens) OnDocEnd()           {}
func (DefaultLintRuleOverTokens) OnEnd()              {}

func (DefaultLintRuleOverTokens) OnNext(tok Token) {}

// LintRuleOverEvents defines how rules over events operate.
type LintRuleOverEvents interface {
	commonLintRule

	// OnLinkByXref is called when a link by label is read.
	OnLinkByXref(xref Token)

	// OnLinkByURL is called when a link by URL is read.
	OnLinkByURL(url Token)

	// OnXrefDefinition is called when a link label definition is read.
	OnXrefDefinition(xref, url Token)

	// OnTableOfContents is called when a table of contents is read.
	OnTableOfContents(toc Token)
}

// The DefaultLintRuleOverEvents provides default implementation for the
// LintRuleOverEvents interface.
type DefaultLintRuleOverEvents struct{}

var _ LintRuleOverEvents = (*DefaultLintRuleOverEvents)(nil)

func (DefaultLintRuleOverEvents) OnStart()            {}
func (DefaultLintRuleOverEvents) OnDocStart(doc *Doc) {}
func (DefaultLintRuleOverEvents) OnDocEnd()           {}
func (DefaultLintRuleOverEvents) OnEnd()              {}

func (DefaultLintRuleOverEvents) OnLinkByXref(xref Token)          {}
func (DefaultLintRuleOverEvents) OnLinkByURL(url Token)            {}
func (DefaultLintRuleOverEvents) OnXrefDefinition(xref, url Token) {}
func (DefaultLintRuleOverEvents) OnTableOfContents(toc Token)      {}

// oneToManyOverTokens is a LintRuleOverTokens which simply dispatches to many
// LintRuleOverTokens, therefore allowing to combine multiple rules where only
// one can be plugged in.
type oneToManyOverTokens []LintRuleOverTokens

var _ LintRuleOverTokens = (*oneToManyOverTokens)(nil)

func (rules oneToManyOverTokens) OnStart() {
	for _, rule := range rules {
		rule.OnStart()
	}
}

func (rules oneToManyOverTokens) OnDocStart(doc *Doc) {
	for _, rule := range rules {
		rule.OnDocStart(doc)
	}
}

func (rules oneToManyOverTokens) OnDocEnd() {
	for _, rule := range rules {
		rule.OnDocEnd()
	}
}

func (rules oneToManyOverTokens) OnEnd() {
	for _, rule := range rules {
		rule.OnEnd()
	}
}

func (rules oneToManyOverTokens) OnNext(tok Token) {
	for _, rule := range rules {
		rule.OnNext(tok)
	}
}

// oneToManyOverEvents is a LintRuleOverEvents which simply dispatches to many
// LintRuleOverEvents, therefore allowing to combine multiple rules where only
// one can be plugged in.
type oneToManyOverEvents []LintRuleOverEvents

var _ LintRuleOverEvents = (*oneToManyOverEvents)(nil)

func (rules oneToManyOverEvents) OnStart() {
	for _, rule := range rules {
		rule.OnStart()
	}
}

func (rules oneToManyOverEvents) OnDocStart(doc *Doc) {
	for _, rule := range rules {
		rule.OnDocStart(doc)
	}
}

func (rules oneToManyOverEvents) OnDocEnd() {
	for _, rule := range rules {
		rule.OnDocEnd()
	}
}

func (rules oneToManyOverEvents) OnEnd() {
	for _, rule := range rules {
		rule.OnEnd()
	}
}

func (rules oneToManyOverEvents) OnLinkByXref(xref Token) {
	for _, rule := range rules {
		rule.OnLinkByXref(xref)
	}
}

func (rules oneToManyOverEvents) OnLinkByURL(url Token) {
	for _, rule := range rules {
		rule.OnLinkByURL(url)
	}
}

func (rules oneToManyOverEvents) OnXrefDefinition(xref, url Token) {
	for _, rule := range rules {
		rule.OnXrefDefinition(xref, url)
	}
}

func (rules oneToManyOverEvents) OnTableOfContents(toc Token) {
	for _, rule := range rules {
		rule.OnTableOfContents(toc)
	}
}
