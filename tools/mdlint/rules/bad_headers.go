// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"bytes"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	core.RegisterLintRuleOverTokens(badHeadersName, newBadHeaders)
}

const badHeadersName = "bad-headers"

type badHeaders struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter

	headerTok               core.Token
	headerLn                int
	headerTxt               bytes.Buffer
	docHeaderStack          []map[string]struct{}
	headerDepth             int
	initHeaderFound         bool
	initHeaderErrorReported bool
	skipHeader              bool
}

var _ core.LintRuleOverTokens = (*badHeaders)(nil)

func newBadHeaders(reporter core.Reporter) core.LintRuleOverTokens {
	rule := badHeaders{reporter: reporter}
	rule.reset()
	return &rule
}

func (rule *badHeaders) reset() {
	rule.headerLn = -1
	rule.docHeaderStack = []map[string]struct{}{}
	rule.headerDepth = -1
	rule.initHeaderFound = false
	rule.initHeaderErrorReported = false
	rule.skipHeader = false
}

func (rule *badHeaders) OnDocStart(_ *core.Doc) {
	rule.reset()
}

func (rule *badHeaders) OnNext(tok core.Token) {
	// Find next header
	if tok.Ln != rule.headerLn {
		rule.skipHeader = false
		switch tok.Kind {
		case core.Header:
			rule.headerTok = tok
			rule.headerLn = tok.Ln
			curr_depth := len(tok.Content)

			if curr_depth == 1 {
				rule.skipHeader = true
				if rule.initHeaderFound {
					// If more than one H1 is found, report and skip
					rule.reporter.Warnf(rule.headerTok, "document can contain only one H1 header")
					return
				}
				rule.initHeaderFound = true
			} else if curr_depth == rule.headerDepth-1 {
				// If header depth decreased, pop highest level set of headers
				rule.docHeaderStack = rule.docHeaderStack[:len(rule.docHeaderStack)-1]
			} else if curr_depth == rule.headerDepth+1 {
				// If header depth increased, create new set to track higher level headers
				rule.docHeaderStack = append(rule.docHeaderStack, make(map[string]struct{}))
			} else if curr_depth != rule.headerDepth {
				// Report and skip misnumbered header
				switch curr_depth {
				case 2:
					rule.reporter.Warnf(rule.headerTok, "misnumbered header, must be H2, or H3")
				default:
					rule.reporter.Warnf(rule.headerTok, "misnumbered header, must be H%d, H%d, or H%d", rule.headerDepth-1, rule.headerDepth, rule.headerDepth+1)
				}
				rule.skipHeader = true
				return
			}
			rule.headerDepth = curr_depth
		case core.JinjaStatement, core.JinjaComment, core.Newline:
			return
		default:
			// No tokens should exist before the initial header
			if !rule.initHeaderFound && !rule.initHeaderErrorReported {
				rule.reporter.Warnf(tok, "H1 should not be preceded by any text")
				rule.initHeaderErrorReported = true
			}
		}
	} else if !rule.skipHeader {
		// Parse in header
		switch tok.Kind {
		case core.Newline, core.EOF:
			// Warn if complete header is a duplicate of a sibling header
			// Otherwise, store header
			header := strings.ToLower(rule.headerTxt.String())
			rule.headerTxt.Reset()
			if _, ok := rule.docHeaderStack[rule.headerDepth-2][header]; ok {
				rule.reporter.Warnf(rule.headerTok, "duplicate header found: %s", header)
				return
			}
			rule.docHeaderStack[rule.headerDepth-2][header] = struct{}{}
		case core.Space:
			return
		default:
			rule.headerTxt.WriteString(tok.Content)
		}
	}
}
