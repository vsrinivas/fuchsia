// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fmt"
	"regexp"
)

var tocRe = regexp.MustCompile(`(?i)^\[toc\]$`)

// The recognizer is a LintRuleOverTokens which recognizes patterns of tokens
// so that it can turn them into patterns and drive a LintRuleOverPatterns.
type recognizer struct {
	rule LintRuleOverPatterns

	state     stepsToRecognizeLinks
	accTokens []Token
}

var _ LintRuleOverTokens = (*recognizer)(nil)

func (r *recognizer) OnStart() {
	r.rule.OnStart()
}

func (r *recognizer) OnDocStart(doc *Doc) {
	r.rule.OnDocStart(doc)
}

func (r *recognizer) OnDocEnd() {
	r.resetState()
	r.rule.OnDocEnd()
}

func (r *recognizer) OnEnd() {
	r.rule.OnEnd()
}

// stepsToRecognizeLinks captures the non-terminal states required to recognize
// the grammar:
//
// Link Link ... -> link with xref
// Link ...       -> link with xref
// Link URL ...  -> link with url
// (on a new line) [ Space ] Link [ Space ] Text(= ":") [ Space ] Text [ Space ] ( Newline | EOF)
//                 -> xref definition
// (on a new line) [ Space ] Link(~ "toc")
//                 -> table of content
//
// Each step represents progression in the state machine towards a possible
// terminal state, or failing so, simply resetting.

type stepsToRecognizeLinks int

const (
	s_Newline stepsToRecognizeLinks = iota
	s_any
	s_Link
	s_Link_Newline
	s_Newline_Link
	s_Newline_Link_TextIsColon
)

var stepsToRecognizeLinksStrings = map[stepsToRecognizeLinks]string{
	s_Newline:                  "s_Newline",
	s_any:                      "s_any",
	s_Link:                     "s_Link",
	s_Link_Newline:             "s_Link_Newline",
	s_Newline_Link:             "s_Newline_Link",
	s_Newline_Link_TextIsColon: "s_Newline_Link_TextIsColon",
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

func (r *recognizer) OnNext(tok Token) {
	switch tok.Kind {
	case Space:
		return // ignore
	case Link, URL:
		r.accTokens = append(r.accTokens, tok)
	}

	switch r.state {
	case s_any:
		switch tok.Kind {
		case Link:
			r.state = s_Link
		case Newline:
			r.state = s_Newline
		default:
			r.resetState()
		}
	case s_Link:
		switch tok.Kind {
		case Link:
			r.rule.OnLinkByXref(tok)
			r.resetState()
		case URL:
			r.rule.OnLinkByURL(tok)
			r.resetState()
		case Newline:
			r.state = s_Link_Newline
		default:
			r.rule.OnLinkByXref(r.accTokens[0])
			r.resetState()
		}
	case s_Link_Newline:
		switch tok.Kind {
		case URL:
			r.rule.OnLinkByURL(tok)
			r.resetState()
		case Link:
			r.rule.OnLinkByXref(r.accTokens[0])
			r.accTokens = r.accTokens[1:]
			r.state = s_Newline_Link
		case Newline:
			// We missed a use in the accumulated tokens, report it now.
			r.rule.OnLinkByXref(r.accTokens[0])
			r.accTokens = nil
			r.state = s_Newline
		default:
			// We missed a use in the accumulated tokens, report it now.
			r.rule.OnLinkByXref(r.accTokens[0])
			r.resetState()
		}
	case s_Newline:
		switch tok.Kind {
		case Link:
			if tocRe.MatchString(tok.Content) {
				r.rule.OnTableOfContents(tok)
				r.resetState()
			} else {
				r.state = s_Newline_Link
			}
		case Newline:
			r.state = s_Newline
		default:
			r.resetState()
		}
	case s_Newline_Link:
		switch tok.Kind {
		case Link:
			r.rule.OnLinkByXref(tok)
			r.resetState()
		case Text:
			if tok.Content == ":" {
				r.state = s_Newline_Link_TextIsColon
			} else if tok.Content[0] == ':' {
				r.state = s_Newline_Link_TextIsColon
				r.OnNext(Token{
					Doc:     tok.Doc,
					Kind:    Text,
					Content: tok.Content[1:],
					Ln:      tok.Ln,
					Col:     tok.Col + 1,
				})
			} else {
				r.rule.OnLinkByXref(r.accTokens[0])
				r.resetState()
			}
		case Newline:
			r.state = s_Link_Newline
		case URL:
			r.rule.OnLinkByURL(tok)
			r.resetState()
		default:
			r.rule.OnLinkByXref(r.accTokens[0])
			r.resetState()
		}
	case s_Newline_Link_TextIsColon:
		switch tok.Kind {
		case Text:
			r.rule.OnXrefDefinition(r.accTokens[0], tok)
			r.resetState()
		default:
			r.resetState()
		}
	default:
		panic("unreachable")
	}
}
