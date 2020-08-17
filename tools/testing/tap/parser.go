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

	"go.fuchsia.dev/fuchsia/tools/testing/tap/tokenizer"
)

// Parse parses the given input string into a Document. The input is allowed to contain
// garbage lines; The parser will skip them and parse as much of the input as possible.
// The only exception is that the first line of input must be a TAP version header of the
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
	rtokens := tokens.Raw()
	if rtokens.Peek().Type == tokenizer.TypeEOF {
		return nil, nil
	}

	if rtokens.Peek().Type == tokenizer.TypeSpace {
		return parseYAMLBlock, nil
	}

	if rtokens.Peek().Type == tokenizer.TypeNumber {
		return parsePlan, nil
	}

	if rtokens.Peek().Value == "ok" || rtokens.Peek().Value == "not" {
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
	return parseNextLine, tokens.Eat(tokenizer.TypeNewline)
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

	if err := tokens.Eat(tokenizer.TypeDot); err != nil {
		return discardLine, err
	}

	if err := tokens.Eat(tokenizer.TypeDot); err != nil {
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
	return parseNextLine, tokens.Eat(tokenizer.TypeNewline)
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
	description := tokens.Raw().ConcatUntil(tokenizer.TypePound, tokenizer.TypeNewline)
	testLine.Description = strings.TrimSpace(description)

	switch tokens.Peek().Type {
	case tokenizer.TypeEOF:
		doc.TestLines = append(doc.TestLines, testLine)
		return nil, nil
	case tokenizer.TypeNewline:
		doc.TestLines = append(doc.TestLines, testLine)
		return discardLine, nil
	case tokenizer.TypePound:
		tokens.Eat(tokenizer.TypePound)
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
	explanation := tokens.Raw().ConcatUntil(tokenizer.TypeNewline)
	testLine.Explanation = strings.TrimSpace(explanation)
	doc.TestLines = append(doc.TestLines, testLine)

	if tokens.Peek().Type == tokenizer.TypeEOF {
		return nil, nil
	}
	tokens.Eat(tokenizer.TypeNewline)
	return parseNextLine, nil
}

// Parses a YAML block. The block must begin as a line containing three dashes and end
// with a line containing three dots.
func parseYAMLBlock(tokens *tokenizer.TokenStream, doc *Document) (state, error) {
	rtokens := tokens.Raw()
	if len(doc.TestLines) == 0 {
		return discardLine, parserError("found YAML with no parent test line")
	}
	testLine := &doc.TestLines[len(doc.TestLines)-1]
	if len(testLine.YAML) > 0 {
		return discardLine, parserError("found YAML with no parent test line")
	}

	// Expect the header to match /\s+---/
	header := rtokens.ConcatUntil(tokenizer.TypeNewline)
	if len(header) < 4 || !strings.HasPrefix(strings.TrimSpace(header), "---") {
		return discardLine, fmt.Errorf("expected line matching /^\\s+---/ but got %q", header)
	}
	if err := rtokens.Eat(tokenizer.TypeNewline); err != nil {
		return discardLine, unexpectedTokenError("a newline", rtokens.Peek())
	}

	var body string
	for {
		line := rtokens.ConcatUntil(tokenizer.TypeNewline)
		// Expect the footer to match /\s+.../
		if len(line) >= 4 && strings.HasPrefix(strings.TrimSpace(line), "...") {
			break
		}

		body += strings.TrimSpace(line) + "\n"
		if rtokens.Peek().Type == tokenizer.TypeEOF {
			break
		}
		rtokens.Eat(tokenizer.TypeNewline)
	}

	testLine.YAML = body
	return parseNextLine, nil
}

func unexpectedTokenError(wanted string, token tokenizer.Token) error {
	return parserError("got %q but wanted %s", token, wanted)
}

func parserError(format string, args ...interface{}) error {
	return fmt.Errorf("parse error: "+format, args...)
}
