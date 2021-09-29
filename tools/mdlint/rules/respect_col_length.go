// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
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
	linkDefState       linkDefState
	isHeaderLine       bool
}

type linkDefState int

const (
	linkDefNone linkDefState = iota
	linkDefLabel
	linkDefColon
)

var _ core.LintRuleOverTokens = (*respectColLength)(nil)

func newRespectColLength(reporter core.Reporter) core.LintRuleOverTokens {
	return &respectColLength{reporter: reporter}
}

// colSizeLimit controls the maximum number of characters on a single line.
const colSizeLimit = 80

func (rule *respectColLength) OnDocStart(_ *core.Doc) {
	rule.countStartingAtCol = 1
	rule.lastReportedLine = 0
	rule.linkDefState = linkDefNone
}

func (rule *respectColLength) OnNext(tok core.Token) {
	// TODO(fxbug.dev/76574): Remove this temporary heuristic.
	switch rule.linkDefState {
	case linkDefNone:
		if tok.Col == 1 && tok.Kind == core.Link {
			rule.linkDefState = linkDefLabel
		}
	case linkDefLabel:
		if tok.Content == ":" {
			rule.linkDefState = linkDefColon
		} else {
			rule.linkDefState = linkDefNone
		}
	case linkDefColon:
		// We've passed `[label]:`, so ignore the rest of the line.
		if tok.Kind != core.Newline {
			return
		}
	}

	switch tok.Kind {
	case core.EOF:
		// Do nothing.
	case core.Newline:
		rule.countStartingAtCol = 1
		rule.linkDefState = linkDefNone
		rule.isHeaderLine = false
	case core.Space, core.List:
		if tok.Col == rule.countStartingAtCol {
			rule.countStartingAtCol += len(tok.Content)
		}
	case core.Header:
		rule.isHeaderLine = true
	// TODO(fxbug.dev/76574): Remove this temporary heuristic.
	case core.URL:
		// Do nothing.
	default:
		if rule.isHeaderLine || tok.Ln == rule.lastReportedLine || tok.Col == rule.countStartingAtCol {
			break
		}
		var length int
		if i := strings.IndexRune(tok.Content, '\n'); i >= 0 {
			length = i
		} else {
			length = len(tok.Content)
		}
		if endCol := tok.Col + length - 1; endCol > colSizeLimit {
			rule.reporter.Warnf(tok, "line over %d column limit", colSizeLimit)
			rule.lastReportedLine = tok.Ln
		}
	}
}
