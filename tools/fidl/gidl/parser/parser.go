// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"io"
	"strconv"
	"strings"
	"text/scanner"

	"gidl/ir"
)

type Parser struct {
	scanner    scanner.Scanner
	lookaheads []token
}

func NewParser(name string, input io.Reader) *Parser {
	var p Parser
	p.scanner.Position.Filename = name
	p.scanner.Init(input)
	return &p
}

func (p *Parser) Parse() (ir.All, error) {
	var result ir.All
	for !p.peekToken(tEof) {
		if err := p.parseSection(&result); err != nil {
			return ir.All{}, err
		}
	}
	return result, nil
}

type tokenKind uint

const (
	_ tokenKind = iota
	tEof
	tText
	tString
	tLacco
	tRacco
	tComma
	tColon
	tNeg
	tLparen
	tRparen
	tEqual
	tLsquare
	tRsquare
)

var tokenKindStrings = []string{
	"<invalid>",
	"<eof>",
	"<text>",
	"<string>",
	"{",
	"}",
	",",
	":",
	"-",
	"(",
	")",
	"=",
	"[",
	"]",
}

var (
	isUnitTokenKind = make(map[tokenKind]bool)
	textToTokenKind = make(map[string]tokenKind)
)

func init() {
	for index, text := range tokenKindStrings {
		if strings.HasPrefix(text, "<") && strings.HasSuffix(text, ">") {
			continue
		}
		kind := tokenKind(index)
		isUnitTokenKind[kind] = true
		textToTokenKind[text] = kind
	}
}

func (kind tokenKind) String() string {
	if index := int(kind); index < len(tokenKindStrings) {
		return tokenKindStrings[index]
	}
	return fmt.Sprintf("%d", kind)
}

type token struct {
	kind         tokenKind
	value        string
	line, column int
}

func (t token) String() string {
	if isUnitTokenKind[t.kind] {
		return t.kind.String()
	} else {
		return t.value
	}
}

type bodyElement uint

const (
	_ bodyElement = iota
	isValue
	isBytes
	isErr
)

var bodyElementStrings = []string{
	"<invalid>",
	"value",
	"bytes",
	"err",
}

func (kind bodyElement) String() string {
	if index := int(kind); index < len(bodyElementStrings) {
		return bodyElementStrings[index]
	} else {
		return fmt.Sprintf("%d", kind)
	}
}

type body struct {
	Value ir.Value
	Bytes []byte
	Err   string
}

type sectionMetadata struct {
	kinds  []bodyElement
	setter func(name string, body body, all *ir.All)
}

var sections = map[string]sectionMetadata{
	"success": {
		kinds: []bodyElement{isValue, isBytes},
		setter: func(name string, body body, all *ir.All) {
			var result ir.Success
			result.Name = name
			result.Value = body.Value
			result.Bytes = body.Bytes
			all.Success = append(all.Success, result)
		},
	},
	"fails_to_encode": {
		kinds: []bodyElement{isValue, isErr},
		setter: func(name string, body body, all *ir.All) {
			var result ir.FailsToEncode
			result.Name = name
			result.Value = body.Value
			result.Err = body.Err
			all.FailsToEncode = append(all.FailsToEncode, result)
		},
	},
	"fails_to_decode": {
		kinds: []bodyElement{isBytes, isErr},
		setter: func(name string, body body, all *ir.All) {
			var result ir.FailsToDecode
			result.Name = name
			result.Bytes = body.Bytes
			result.Err = body.Err
			all.FailsToDecode = append(all.FailsToDecode, result)
		},
	},
}

func (p *Parser) parseSection(all *ir.All) error {
	section, name, err := p.parsePreamble()
	if err != nil {
		return err
	}
	body, err := p.parseBody(section.kinds)
	if err != nil {
		return err
	}
	section.setter(name, body, all)
	return nil
}

func (p *Parser) parsePreamble() (sectionMetadata, string, error) {
	tok, ok := p.consumeToken(tText)
	if !ok {
		return sectionMetadata{}, "", p.failExpectedToken(tText, tok)
	}

	section, ok := sections[tok.value]
	if !ok {
		return sectionMetadata{}, "", p.newParseError(tok, "unknown section %s", tok.value)
	}

	tok, ok = p.consumeToken(tLparen)
	if !ok {
		return sectionMetadata{}, "", p.failExpectedToken(tLparen, tok)
	}

	tok, ok = p.consumeToken(tString)
	if !ok {
		return sectionMetadata{}, "", p.failExpectedToken(tString, tok)
	}
	name := tok.value

	tok, ok = p.consumeToken(tRparen)
	if !ok {
		return sectionMetadata{}, "", p.failExpectedToken(tRparen, tok)
	}

	return section, name, nil
}

func (p *Parser) parseBody(kinds []bodyElement) (body, error) {
	var (
		result      body
		allElements = make(map[bodyElement]bool)
	)
	bodyTok, ok := p.consumeToken(tLacco)
	if !ok {
		return result, p.failExpectedToken(tLacco, bodyTok)
	}
	for !p.peekToken(tRacco) {
		if err := p.parseSingleBodyElement(&result, allElements); err != nil {
			return result, err
		}
	}
	for _, kind := range kinds {
		if !allElements[kind] {
			return result, p.newParseError(bodyTok, "missing %s", kind)
		}
	}
	if tok, ok := p.consumeToken(tRacco); !ok {
		return result, p.failExpectedToken(tRacco, tok)
	}
	return result, nil
}

func (p *Parser) parseSingleBodyElement(result *body, all map[bodyElement]bool) error {
	tok, ok := p.consumeToken(tText)
	if !ok {
		return p.failExpectedToken(tText, tok)
	}
	if tok, ok := p.consumeToken(tEqual); !ok {
		return p.failExpectedToken(tEqual, tok)
	}
	var kind bodyElement
	switch tok.value {
	case "value":
		val, err := p.parseValue()
		if err != nil {
			return err
		}
		result.Value = val
		kind = isValue
	case "bytes":
		bytes, err := p.parseBytes()
		if err != nil {
			return err
		}
		result.Bytes = bytes
		kind = isBytes
	case "err":
		tok, ok := p.consumeToken(tText)
		if !ok {
			return p.failExpectedToken(tText, tok)
		}
		result.Err = tok.value
		kind = isErr
	default:
		return p.newParseError(tok, "must be value, bytes, or err")
	}
	if all[kind] {
		return p.newParseError(tok, "duplicate %s found", kind)
	}
	all[kind] = true
	return nil
}

func (p *Parser) parseValue() (interface{}, error) {
	tok := p.nextToken()
	switch tok.kind {
	case tText:
		if '0' <= tok.value[0] && tok.value[0] <= '9' {
			return parseNum(tok, false)
		}
		if tok.value == "true" {
			return true, nil
		}
		if tok.value == "false" {
			return true, nil
		}
		return p.parseObject(tok.value)
	case tLsquare:
		return p.parseSlice()
	case tString:
		return tok.value, nil
	case tNeg:
		if tok, ok := p.consumeToken(tText); !ok {
			return nil, p.failExpectedToken(tText, tok)
		} else {
			return parseNum(tok, true)
		}
	default:
		return nil, p.newParseError(tok, "expected value")
	}
}

func parseNum(tok token, neg bool) (interface{}, error) {
	if strings.Contains(tok.value, ".") {
		val, err := strconv.ParseFloat(tok.value, 64)
		if err != nil {
			return nil, err
		}
		if neg {
			return -val, nil
		} else {
			return val, nil
		}
	} else {
		val, err := strconv.ParseUint(tok.value, 0, 64)
		if err != nil {
			return nil, err
		}
		if neg {
			return -int64(val), nil
		} else {
			return uint64(val), nil
		}
	}
}

func (p *Parser) parseObject(name string) (interface{}, error) {
	obj := ir.Object{Name: name}
	if tok, ok := p.consumeToken(tLacco); !ok {
		return nil, p.failExpectedToken(tLacco, tok)
	}
	for !p.peekToken(tRacco) {
		tokFieldName, ok := p.consumeToken(tText)
		if !ok {
			return nil, p.failExpectedToken(tText, tokFieldName)
		}
		if tok, ok := p.consumeToken(tColon); !ok {
			return nil, p.failExpectedToken(tColon, tok)
		}
		val, err := p.parseValue()
		if err != nil {
			return nil, err
		}
		if tok, ok := p.consumeToken(tComma); !ok {
			return nil, p.failExpectedToken(tComma, tok)
		}
		if obj.Fields == nil {
			obj.Fields = make(map[string]ir.Value)
		}
		obj.Fields[tokFieldName.value] = val
	}
	if tok, ok := p.consumeToken(tRacco); !ok {
		return nil, p.failExpectedToken(tRacco, tok)
	}
	return obj, nil
}

// TODO(pascallouis): parseSlice() expects that the opening [ has already been
// consumed while parseObject and parseBytes expect to have to consume { and [
// respectively. That's confusing, make parseSlice behave like the others.
func (p *Parser) parseSlice() ([]interface{}, error) {
	var result []interface{}
	for !p.peekToken(tRsquare) {
		val, err := p.parseValue()
		if err != nil {
			return nil, err
		}
		if tok, ok := p.consumeToken(tComma); !ok {
			return nil, p.failExpectedToken(tComma, tok)
		}
		result = append(result, val)
	}
	if tok, ok := p.consumeToken(tRsquare); !ok {
		return nil, p.failExpectedToken(tRsquare, tok)
	}
	return result, nil
}

func (p *Parser) parseBytes() ([]byte, error) {
	if tok, ok := p.consumeToken(tLacco); !ok {
		return nil, p.failExpectedToken(tLacco, tok)
	}
	var bytes []byte
	for !p.peekToken(tRacco) {
		lit, err := p.parseByte()
		if err != nil {
			return nil, err
		}
		bytes = append(bytes, lit)
		if tok, ok := p.consumeToken(tComma); !ok {
			return nil, p.failExpectedToken(tComma, tok)
		}
	}
	if tok, ok := p.consumeToken(tRacco); !ok {
		return nil, p.failExpectedToken(tRacco, tok)
	}
	return bytes, nil
}

func (p *Parser) parseByte() (byte, error) {
	tok, ok := p.consumeToken(tText)
	if !ok {
		return 0, p.failExpectedToken(tText, tok)
	}
	if len(tok.value) == 3 && tok.value[0] == '\'' && tok.value[2] == '\'' {
		return tok.value[1], nil
	}
	b, err := strconv.ParseUint(tok.value, 0, 8)
	if err != nil {
		return 0, p.newParseError(tok, err.Error())
	}
	return byte(b), nil
}

func (p *Parser) consumeToken(kind tokenKind) (token, bool) {
	tok := p.nextToken()
	return tok, tok.kind == kind
}

func (p *Parser) peekToken(kind tokenKind) bool {
	if len(p.lookaheads) == 0 {
		tok := p.nextToken()
		p.lookaheads = append(p.lookaheads, tok)
	}
	return p.lookaheads[0].kind == kind
}

func (p *Parser) nextToken() token {
	if len(p.lookaheads) != 0 {
		var tok token
		tok, p.lookaheads = p.lookaheads[0], p.lookaheads[1:]
		return tok
	}
	return p.scanToken()
}

func (p *Parser) scanToken() token {
	// eof
	if tok := p.scanner.Scan(); tok == scanner.EOF {
		return token{tEof, "", 0, 0}
	}
	pos := p.scanner.Position

	// unit tokens
	text := p.scanner.TokenText()
	if kind, ok := textToTokenKind[text]; ok {
		return token{kind, text, pos.Line, pos.Column}
	}

	// string
	if text[0] == '"' {
		// TODO escape
		return token{tString, text[1 : len(text)-1], pos.Line, pos.Column}
	}

	// text
	return token{tText, text, pos.Line, pos.Column}
}

type parseError struct {
	input        string
	line, column int
	message      string
}

// Assert parseError implements error interface
var _ error = &parseError{}

func (err *parseError) Error() string {
	return fmt.Sprintf("%s:%d:%d: %s", err.input, err.line, err.column, err.message)
}

func (p *Parser) failExpectedToken(expected tokenKind, found token) error {
	return p.newParseError(found, "expected %s found %s", expected, found)
}

func (p *Parser) newParseError(tok token, format string, a ...interface{}) error {
	return &parseError{
		input:   p.scanner.Position.Filename,
		line:    tok.line,
		column:  tok.column,
		message: fmt.Sprintf(format, a...),
	}
}
