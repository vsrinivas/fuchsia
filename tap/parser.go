// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tap

import (
	"errors"
	"fmt"
	"log"
	"strconv"
	"strings"

	"fuchsia.googlesource.com/tools/tap/tokenizer"
)

// Parse parses the given input string into a Document. The input is allowed to contain
// garbage lines; The parser will skip them and parse as much of the input as possible.
// The only execption is that the first line of input must be a TAP version header of the
// form "TAP version XXX".
func Parse(input []byte) (*Document, error) {
	output := make(chan *Document)
	go parse(tokenizer.NewTokenStream(input), output)
	return <-output, nil
}

// State represents a parser state. Each state takes the current stream of input tokens
// and the current Document and attempts to parse the next line of input. A state must
// return the next state to use, even when an error is encountered. If nil is returned,
// parsing stops.
type state func(*tokenizer.TokenStream, *Document) (state, error)

// Parse parses a Document from the given Token stream. The result is emitted on the
// output channel.
func parse(tokens *tokenizer.TokenStream, output chan<- *Document) {
	document := &Document{}

	for state := parseVersion; state != nil; {
		next, err := state(tokens, document)
		if err != nil {
			// Garbage lines are allowed; Treat errors as non-fatal.
			log.Println(err)
		}
		state = next
	}

	output <- document
}

// DiscardLine is a parser state that throws away every token until a newline or EOF.
func discardLine(tokens *tokenizer.TokenStream, _ *Document) (state, error) {
	for {
		token := tokens.Peek()
		switch {
		case token.Type == tokenizer.TypeEOF:
			return nil, nil
		case token.Type != tokenizer.TypeNewline:
			tokens.Next()
		default:
			tokens.Next() // Skip the newline.
			return parseNextLine, nil
		}
	}
}

func parseNextLine(tokens *tokenizer.TokenStream, doc *Document) (state, error) {
	if tokens.Peek().Type == tokenizer.TypeEOF {
		return nil, nil
	}

	if tokens.Peek().Type == tokenizer.TypeNumber {
		return parsePlan, nil
	}

	if tokens.Peek().Value == "ok" || tokens.Peek().Value == "not" {
		return parseTestLine, nil
	}

	return parseNextLine, unexpectedTokenError("one of 'ok', 'not' or a number", tokens.Next())
}

func parseVersion(tokens *tokenizer.TokenStream, doc *Document) (state, error) {
	token := tokens.Next()
	if token.Value != "TAP" {
		return nil, unexpectedTokenError("'TAP'", token)
	}

	token = tokens.Next()
	if token.Value != "version" {
		return nil, unexpectedTokenError("'version'", token)
	}

	token = tokens.Next()
	if token.Type != tokenizer.TypeNumber {
		return nil, unexpectedTokenError("a version number", token)
	}

	version, err := strconv.ParseInt(token.Value, 10, 64)
	if err != nil {
		return nil, parserError(err.Error())
	}

	doc.Version = Version(version)
	return parseNextLine, eat(tokens, tokenizer.TypeNewline)
}

func parsePlan(tokens *tokenizer.TokenStream, doc *Document) (state, error) {
	if doc.Plan.Start != 0 || doc.Plan.End != 0 {
		return discardLine, errors.New("plan has already been parsed")
	}

	token := tokens.Peek()
	if token.Type != tokenizer.TypeNumber {
		return discardLine, unexpectedTokenError("a number", token)
	}

	start, err := strconv.ParseInt(tokens.Next().Value, 10, 64)
	if err != nil {
		return discardLine, parserError(err.Error())
	}

	if err := eat(tokens, tokenizer.TypeDot); err != nil {
		return discardLine, err
	}

	if err := eat(tokens, tokenizer.TypeDot); err != nil {
		return discardLine, err
	}

	token = tokens.Peek()
	if token.Type != tokenizer.TypeNumber {
		return discardLine, unexpectedTokenError("a number > 1", token)
	}

	end, err := strconv.ParseInt(tokens.Next().Value, 10, 64)
	if err != nil {
		return discardLine, parserError(err.Error())
	}

	doc.Plan = Plan{Start: int(start), End: int(end)}
	return parseNextLine, eat(tokens, tokenizer.TypeNewline)
}

func parseTestLine(tokens *tokenizer.TokenStream, doc *Document) (state, error) {
	var testLine TestLine

	// Parse test status.
	token := tokens.Next()
	switch token.Value {
	case "not":
		testLine.Ok = false
		token = tokens.Next()
		if token.Value != "ok" {
			return discardLine, unexpectedTokenError("'ok'", token)
		}
	case "ok":
		testLine.Ok = true
	default:
		return discardLine, unexpectedTokenError("'ok' or 'not ok'", token)
	}

	// Parse optional test number.
	testLine.Count = len(doc.TestLines) + 1
	if tokens.Peek().Type == tokenizer.TypeNumber {
		count, err := strconv.ParseInt(tokens.Next().Value, 10, 64)
		if err != nil {
			return discardLine, parserError(err.Error())
		}
		testLine.Count = int(count)
	}

	// Parse optional description. Stop at a TypePound token which marks the start of a
	// diagnostic.
	testLine.Description = concat(tokens.Raw(), func(tok tokenizer.Token) bool {
		t := tok.Type
		return t != tokenizer.TypePound && t != tokenizer.TypeNewline && t != tokenizer.TypeEOF
	})

	// Move to next line if there's no directive.
	if err := eat(tokens, tokenizer.TypePound); err != nil {
		doc.TestLines = append(doc.TestLines, testLine)
		return discardLine, err
	}

	// Parse optional directive.
	token = tokens.Next()
	switch token.Value {
	case "TODO":
		testLine.Directive = Todo
	case "SKIP":
		testLine.Directive = Skip
	default:
		return discardLine, unexpectedTokenError("a directive", token)
	}

	// Parse explanation.
	testLine.Explanation = concat(tokens.Raw(), func(tok tokenizer.Token) bool {
		t := tok.Type
		return t != tokenizer.TypeNewline && t != tokenizer.TypeEOF
	})

	doc.TestLines = append(doc.TestLines, testLine)
	return parseNextLine, eat(tokens, tokenizer.TypeNewline)
}

// Eat consumes the next token from the stream iff it's type matches typ. If the types
// are different, an error is returned.
func eat(tokens *tokenizer.TokenStream, typ tokenizer.TokenType) error {
	token := tokens.Peek()
	if token.Type != typ {
		return unexpectedTokenError(string(typ), token)
	}
	tokens.Next()
	return nil
}

// Concat concatenates the values of the next tokens in the stream as long as cond keeps
// returning true. Returns the contatenated output with leading and trailing spaces
// trimmed.
func concat(tokens *tokenizer.RawTokenStream, cond func(tok tokenizer.Token) bool) string {
	var values string
	for cond(tokens.Peek()) {
		values += tokens.Next().Value
	}
	return strings.TrimSpace(values)
}

func unexpectedTokenError(wanted string, token tokenizer.Token) error {
	return parserError("got %q but wanted %s", token, wanted)
}

func parserError(format string, args ...interface{}) error {
	return fmt.Errorf("parse error: "+format, args...)
}
