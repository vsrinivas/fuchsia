// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"regexp"
)

var tocRe = regexp.MustCompile(`(?i)^\[toc\]$`)

// The recognizer is a lintRuleOverTokens which recognizes patterns of tokens
// so that it can turn them into events and drive a lintRuleOverEvents.
type recognizer struct {
	rule lintRuleOverEvents

	state     stepsToRecognizeLinks
	accTokens []token
}

var _ lintRuleOverTokens = (*recognizer)(nil)

func (r *recognizer) onStart() {
	r.rule.onStart()
}

func (r *recognizer) onDocStart(doc *doc) {
	r.rule.onDocStart(doc)
}

func (r *recognizer) onDocEnd() {
	r.resetState()
	r.rule.onDocEnd()
}

func (r *recognizer) onEnd() {
	r.rule.onEnd()
}

// stepsToRecognizeLinks captures the non-terminal states required to recognize
// the grammar:
//
// tLink tLink ... -> link with xref
// tLink ...       -> link with xref
// tLink tURL ...  -> link with url
// (on a new line) [ tSpace ] tLink [ tSpace ] tText(= ":") [ tSpace ] tText [ tSpace ] ( tNewline | tEOF)
//                 -> xref definition
// (on a new line) [ tSpace ] tLink(~ "toc")
//                 -> table of content
//
// Each step represents progression in the state machine towards a possible
// terminal state, or failing so, simply resetting.

type stepsToRecognizeLinks int

const (
	s_tNewline stepsToRecognizeLinks = iota
	s_any
	s_tLink
	s_tLink_tNewline
	s_tNewline_tLink
	s_tNewline_tLink_tTextIsColon
)

var stepsToRecognizeLinksStrings = map[stepsToRecognizeLinks]string{
	s_tNewline:                    "s_tNewline",
	s_any:                         "s_any",
	s_tLink:                       "s_tLink",
	s_tLink_tNewline:              "s_tLink_tNewline",
	s_tNewline_tLink:              "s_tNewline_tLink",
	s_tNewline_tLink_tTextIsColon: "s_tNewline_tLink_tTextIsColon",
}

func (s stepsToRecognizeLinks) String() string {
	if fmt, ok := stepsToRecognizeLinksStrings[s]; ok {
		return fmt
	}
	return fmt.Sprintf("stepsToRecognizeLinks(unknown: %d)", s)
}

func (r *recognizer) resetState() {
	r.state = s_any
	r.accTokens = nil
}

func (r *recognizer) onNext(tok token) {
	switch tok.kind {
	case tSpace:
		return // ignore
	case tLink, tURL:
		r.accTokens = append(r.accTokens, tok)
	}

	switch r.state {
	case s_any:
		switch tok.kind {
		case tLink:
			r.state = s_tLink
		case tNewline:
			r.state = s_tNewline
		default:
			r.resetState()
		}
	case s_tLink:
		switch tok.kind {
		case tLink:
			r.rule.onLinkByXref(tok)
			r.resetState()
		case tURL:
			r.rule.onLinkByURL(tok)
			r.resetState()
		case tNewline:
			r.state = s_tLink_tNewline
		default:
			r.rule.onLinkByXref(r.accTokens[0])
			r.resetState()
		}
	case s_tLink_tNewline:
		switch tok.kind {
		case tURL:
			r.rule.onLinkByURL(tok)
			r.resetState()
		case tLink:
			r.rule.onLinkByXref(r.accTokens[0])
			r.accTokens = r.accTokens[1:]
			r.state = s_tNewline_tLink
		case tNewline:
			// We missed a use in the accumulated tokens, report it now.
			r.rule.onLinkByXref(r.accTokens[0])
			r.accTokens = nil
			r.state = s_tNewline
		default:
			// We missed a use in the accumulated tokens, report it now.
			r.rule.onLinkByXref(r.accTokens[0])
			r.resetState()
		}
	case s_tNewline:
		switch tok.kind {
		case tLink:
			if tocRe.MatchString(tok.content) {
				r.rule.onTableOfContents(tok)
				r.resetState()
			} else {
				r.state = s_tNewline_tLink
			}
		case tNewline:
			r.state = s_tNewline
		default:
			r.resetState()
		}
	case s_tNewline_tLink:
		switch tok.kind {
		case tText:
			if tok.content == ":" {
				r.state = s_tNewline_tLink_tTextIsColon
			} else if tok.content[0] == ':' {
				r.state = s_tNewline_tLink_tTextIsColon
				r.onNext(token{
					doc:     tok.doc,
					kind:    tText,
					content: tok.content[1:],
					ln:      tok.ln,
					col:     tok.col + 1,
				})
			} else {
				r.rule.onLinkByXref(r.accTokens[0])
				r.resetState()
			}
		case tNewline:
			r.state = s_tLink_tNewline
		case tURL:
			r.rule.onLinkByURL(tok)
			r.resetState()
		default:
			r.rule.onLinkByXref(r.accTokens[0])
			r.resetState()
		}
	case s_tNewline_tLink_tTextIsColon:
		switch tok.kind {
		case tText:
			r.rule.onXrefDefinition(r.accTokens[0], tok)
			r.resetState()
		default:
			r.resetState()
		}
	default:
		panic("unreachable")
	}
}
