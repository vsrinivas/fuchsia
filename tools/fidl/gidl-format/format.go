// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io"
	"strings"
	"text/scanner"
)

const (
	indentWidth    = 4
	disableComment = "// gidl-format off"
	enableComment  = "// gidl-format on"
)

type formatter struct {
	scanner.Scanner
	enabled bool
	err     error
	// Stack of open brackets that have not yet been closed.
	brackets []rune
}

// format formats GIDL syntax from src to dst.
// It uses filename for error messages about src.
func format(dst io.StringWriter, src io.Reader, filename string) error {
	var f formatter
	f.Init(src)
	f.Filename = filename
	// Don't skip comments.
	f.Mode &^= scanner.SkipComments
	f.Error = func(s *scanner.Scanner, msg string) {
		f.fail(msg)
	}
	f.enable()
	f.write(dst)
	return f.err
}

func (f *formatter) fail(format string, args ...interface{}) {
	f.err = fmt.Errorf("%s: %s", f.Position, fmt.Sprintf(format, args...))
}

func (f *formatter) enable() {
	f.enabled = true
	// Skip all whitespace except newlines.
	f.Whitespace = scanner.GoWhitespace &^ (1 << '\n')
}

func (f *formatter) disable() {
	f.enabled = false
	// Preserve whitespace while disabled.
	f.Whitespace = 0
}

func (f *formatter) write(dst io.StringWriter) {
	var (
		prev            rune
		pendingNewlines int
	)
	for tok := f.Scan(); tok != scanner.EOF && f.err == nil; tok = f.Scan() {
		// Keep track of bracket nesting, whether formatting is enabled or not.
		enclosingBracket := f.innermostBracket()
		if ok := f.updateBrackets(tok); !ok {
			break
		}
		// Toggle enabled/disabled based on special comments.
		if f.enabled {
			if tok == scanner.Comment && f.TokenText() == disableComment {
				f.disable()
				// Reset prev to avoid incorrect spacing after re-enabling.
				prev = 0
			}
		} else {
			if tok == scanner.Comment && f.TokenText() == enableComment {
				f.enable()
			}
			// While disabled, copy the input unchanged.
			dst.WriteString(f.TokenText())
			continue
		}
		// Count newlines but don't print them yet.
		if tok == '\n' {
			pendingNewlines++
			continue
		}
		// Enforce trailing commas when there is a line break.
		if pendingNewlines > 0 && prev != 0 && needCommaBetween(prev, tok) {
			dst.WriteString(",")
		}
		// Delete trailing commas when there is no line break.
		if tok == ',' && isCloseBracket(f.Peek()) {
			continue
		}
		// Having reached a non-newline character, print the correct number of
		// newlines. Don't print newlines between empty brackets.
		wroteNewline := false
		if pendingNewlines >= 1 && !(isOpenBracket(prev) && tok == closeBracket(prev)) {
			wroteNewline = true
			dst.WriteString("\n")
			// Collapse multiple blank lines to a single blank line.
			if pendingNewlines >= 2 {
				dst.WriteString("\n")
			}
		}
		pendingNewlines = 0
		// Enforce a blank line between top-level declarations.
		if len(f.brackets) == 0 && tok == '}' {
			pendingNewlines = 2
		}
		// Add whitespace before the token.
		if prev == 0 || wroteNewline {
			depth := len(f.brackets)
			// At the start of a line, a close bracket should dedent itself, but
			// an open bracket should not indent itself.
			if isOpenBracket(tok) {
				depth--
			}
			dst.WriteString(strings.Repeat(" ", indentWidth*depth))
		} else if needSpaceBetween(prev, tok, enclosingBracket) {
			dst.WriteString(" ")
		}
		// Finally, write the token itself.
		dst.WriteString(f.TokenText())
		prev = tok
	}
	// End the file with a single newline.
	if f.err == nil && f.enabled {
		dst.WriteString("\n")
	}
}

func (f *formatter) innermostBracket() rune {
	if len(f.brackets) == 0 {
		return 0
	}
	return f.brackets[len(f.brackets)-1]
}

func (f *formatter) updateBrackets(tok rune) bool {
	if isOpenBracket(tok) {
		f.brackets = append(f.brackets, tok)
	} else if isCloseBracket(tok) {
		if len(f.brackets) == 0 {
			f.fail("extraenous closing bracket '%c'", tok)
			return false
		}
		var open rune
		open, f.brackets = f.brackets[len(f.brackets)-1], f.brackets[:len(f.brackets)-1]
		if tok != closeBracket(open) {
			f.fail("mismatched closing bracket '%c' (expected '%c')", tok, closeBracket(open))
			return false
		}
	}
	return true
}

func isOpenBracket(tok rune) bool {
	return tok == '(' || tok == '[' || tok == '{'
}

func isCloseBracket(tok rune) bool {
	return tok == ')' || tok == ']' || tok == '}'
}

func closeBracket(open rune) rune {
	switch open {
	case '(':
		return ')'
	case '[':
		return ']'
	case '{':
		return '}'
	default:
		panic("invalid open bracket")
	}
}

// needCommaBetween assumes there is a newline between lhs and rhs, and returns
// true if there should be a comma before the newline.
func needCommaBetween(lhs, rhs rune) bool {
	if lhs == 0 || rhs == 0 || lhs == '\n' || rhs == '\n' {
		panic("invalid character")
	}
	switch lhs {
	case ',', scanner.Comment:
		return false
	}
	return !isOpenBracket(lhs) && isCloseBracket(rhs)
}

// needSpaceBetween returns true if there should be a space between lhs and rhs,
// assuming the most recent open bracket before lhs was enclosingBracket.
func needSpaceBetween(lhs, rhs rune, enclosingBracket rune) bool {
	if lhs == 0 || rhs == 0 || lhs == '\n' || rhs == '\n' {
		panic("invalid character")
	}
	// Add a space after lhs?
	switch lhs {
	case ',', '=', '+':
		return true
	case '-':
		// Add space if it's a binary operation (handle rights).
		return rhs == scanner.Ident
	case ':':
		// Don't add a space for the byte syntax like "padding:3" or
		// "repeat(0xff):8", which occurs in a square bracket list.
		return enclosingBracket != '['
	}
	// Add a space before rhs?
	switch rhs {
	case '=', '+', scanner.Comment:
		return true
	case '-':
		// Add space if it's a binary operation (handle rights).
		return lhs == scanner.Ident
	case '{':
		// This adds the space in `success("Foo") {`.
		return lhs == ')'
	}
	return false
}
