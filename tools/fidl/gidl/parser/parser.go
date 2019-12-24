// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"bytes"
	"encoding/binary"
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
	// All supported languages, used to validate bindings allowlist/denylist.
	allLanguages ir.LanguageList
}

func NewParser(name string, input io.Reader, allLanguages []string) *Parser {
	var p Parser
	p.scanner.Position.Filename = name
	p.scanner.Init(input)
	p.allLanguages = allLanguages
	return &p
}

func (p *Parser) Parse() (ir.All, error) {
	var result ir.All
	for !p.peekTokenKind(tEof) {
		if err := p.parseSection(&result); err != nil {
			return ir.All{}, err
		}
	}
	// TODO(FIDL-754) Add validation checks for error codes after parsing.
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
	isType
	isValue
	isBytes
	isErr
	isBindingsAllowlist
	isBindingsDenylist
)

func (kind bodyElement) String() string {
	switch kind {
	case isType:
		return "type"
	case isValue:
		return "value"
	case isBytes:
		return "bytes"
	case isErr:
		return "err"
	case isBindingsAllowlist:
		return "bindings_allowlist"
	case isBindingsDenylist:
		return "bindings_denylist"
	default:
		panic("unsupported kind")
	}
}

type body struct {
	Type              string
	Value             ir.Value
	Encodings         []ir.Encoding
	Err               ir.ErrorCode
	BindingsAllowlist *ir.LanguageList
	BindingsDenylist  *ir.LanguageList
}

type sectionMetadata struct {
	requiredKinds map[bodyElement]bool
	optionalKinds map[bodyElement]bool
	setter        func(name string, body body, all *ir.All)
}

var sections = map[string]sectionMetadata{
	"success": {
		requiredKinds: map[bodyElement]bool{isValue: true, isBytes: true},
		optionalKinds: map[bodyElement]bool{isBindingsAllowlist: true, isBindingsDenylist: true},
		setter: func(name string, body body, all *ir.All) {
			encodeSuccess := ir.EncodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.EncodeSuccess = append(all.EncodeSuccess, encodeSuccess)
			decodeSuccess := ir.DecodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.DecodeSuccess = append(all.DecodeSuccess, decodeSuccess)
		},
	},
	"encode_success": {
		requiredKinds: map[bodyElement]bool{isValue: true, isBytes: true},
		optionalKinds: map[bodyElement]bool{isBindingsAllowlist: true},
		setter: func(name string, body body, all *ir.All) {
			result := ir.EncodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				BindingsAllowlist: body.BindingsAllowlist,
			}
			all.EncodeSuccess = append(all.EncodeSuccess, result)
		},
	},
	"decode_success": {
		requiredKinds: map[bodyElement]bool{isValue: true, isBytes: true},
		optionalKinds: map[bodyElement]bool{isBindingsAllowlist: true},
		setter: func(name string, body body, all *ir.All) {
			result := ir.DecodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				BindingsAllowlist: body.BindingsAllowlist,
			}
			all.DecodeSuccess = append(all.DecodeSuccess, result)
		},
	},
	"encode_failure": {
		requiredKinds: map[bodyElement]bool{isValue: true, isErr: true},
		optionalKinds: map[bodyElement]bool{isBindingsAllowlist: true, isBindingsDenylist: true},
		setter: func(name string, body body, all *ir.All) {
			result := ir.EncodeFailure{
				Name:              name,
				Value:             body.Value,
				WireFormats:       ir.AllWireFormats(),
				Err:               body.Err,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.EncodeFailure = append(all.EncodeFailure, result)
		},
	},
	"decode_failure": {
		requiredKinds: map[bodyElement]bool{isType: true, isBytes: true, isErr: true},
		optionalKinds: map[bodyElement]bool{isBindingsAllowlist: true, isBindingsDenylist: true},
		setter: func(name string, body body, all *ir.All) {
			result := ir.DecodeFailure{
				Name:              name,
				Type:              body.Type,
				Encodings:         body.Encodings,
				Err:               body.Err,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.DecodeFailure = append(all.DecodeFailure, result)
		},
	},
}

func (p *Parser) parseSection(all *ir.All) error {
	section, name, err := p.parsePreamble()
	if err != nil {
		return err
	}
	body, err := p.parseBody(section.requiredKinds, section.optionalKinds)
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

func (p *Parser) parseBody(requiredKinds map[bodyElement]bool, optionalKinds map[bodyElement]bool) (body, error) {
	var (
		result      body
		parsedKinds = make(map[bodyElement]bool)
		bodyTok     = p.peekToken()
	)
	err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		return p.parseSingleBodyElement(&result, parsedKinds)
	})
	if err != nil {
		return result, err
	}
	for requiredKind := range requiredKinds {
		if !parsedKinds[requiredKind] {
			return result, p.newParseError(bodyTok, "missing required parameter '%s'", requiredKind)
		}
	}
	for parsedKind := range parsedKinds {
		if !requiredKinds[parsedKind] && !optionalKinds[parsedKind] {
			return result, p.newParseError(bodyTok, "parameter '%s' does not apply to element", parsedKind)
		}
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
	case "type":
		tok, ok := p.consumeToken(tText)
		if !ok {
			return p.failExpectedToken(tText, tok)
		}
		result.Type = tok.value
		kind = isType
	case "value":
		val, err := p.parseValue()
		if err != nil {
			return err
		}
		result.Value = val
		kind = isValue
	case "bytes":
		encodings, err := p.parseByteSection()
		if err != nil {
			return err
		}
		result.Encodings = encodings
		kind = isBytes
	case "err":
		errorCode, err := p.parseErrorCode()
		if err != nil {
			return err
		}
		result.Err = errorCode
		kind = isErr
	case "bindings_allowlist":
		languages, err := p.parseLanguageList()
		if err != nil {
			return err
		}
		result.BindingsAllowlist = &languages
		kind = isBindingsAllowlist
	case "bindings_denylist":
		languages, err := p.parseLanguageList()
		if err != nil {
			return err
		}
		result.BindingsDenylist = &languages
		kind = isBindingsDenylist
	default:
		return p.newParseError(tok, "must be type, value, bytes, err, bindings_allowlist or bindings_denylist")
	}
	if all[kind] {
		return p.newParseError(tok, "duplicate %s found", kind)
	}
	all[kind] = true
	return nil
}

func (p *Parser) parseValue() (interface{}, error) {
	switch p.peekToken().kind {
	case tText:
		tok := p.nextToken()
		if '0' <= tok.value[0] && tok.value[0] <= '9' {
			return parseNum(tok, false)
		}
		if tok.value == "null" {
			return nil, nil
		}
		if tok.value == "true" {
			return true, nil
		}
		if tok.value == "false" {
			return false, nil
		}
		return p.parseObject(tok.value)
	case tLsquare:
		return p.parseSlice()
	case tString:
		return p.nextToken().value, nil
	case tNeg:
		p.nextToken()
		if tok, ok := p.consumeToken(tText); !ok {
			return nil, p.failExpectedToken(tText, tok)
		} else {
			return parseNum(tok, true)
		}
	default:
		return nil, p.newParseError(p.peekToken(), "expected value")
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
	err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		tokFieldName, ok := p.consumeToken(tText)
		if !ok {
			return p.failExpectedToken(tText, tokFieldName)
		}
		if tok, ok := p.consumeToken(tColon); !ok {
			return p.failExpectedToken(tColon, tok)
		}
		val, err := p.parseValue()
		if err != nil {
			return err
		}
		obj.Fields = append(obj.Fields, ir.Field{Key: decodeFieldKey(tokFieldName.value), Value: val})
		return nil
	})
	if err != nil {
		return nil, err
	}
	return obj, nil
}

// Field can be referenced by either name or ordinal.
func decodeFieldKey(field string) ir.FieldKey {
	if ord, err := strconv.ParseInt(field, 0, 64); err == nil {
		return ir.FieldKey{Ordinal: uint64(ord)}
	}
	return ir.FieldKey{Name: field}
}

func (p *Parser) parseErrorCode() (ir.ErrorCode, error) {
	tok, ok := p.consumeToken(tText)
	if !ok {
		return "", p.failExpectedToken(tText, tok)
	}
	code := ir.ErrorCode(tok.value)
	if _, ok := ir.AllErrorCodes[code]; !ok {
		return "", p.newParseError(tok, "unknown error code")
	}
	return code, nil
}

func (p *Parser) parseSlice() ([]interface{}, error) {
	var result []interface{}
	err := p.parseCommaSeparated(tLsquare, tRsquare, func() error {
		val, err := p.parseValue()
		if err != nil {
			return err
		}
		result = append(result, val)
		return nil
	})
	if err != nil {
		return nil, err
	}
	return result, nil
}

func (p *Parser) parseTextSlice() ([]string, error) {
	var result []string
	err := p.parseCommaSeparated(tLsquare, tRsquare, func() error {
		if tok, ok := p.consumeToken(tText); !ok {
			return p.failExpectedToken(tText, tok)
		} else {
			result = append(result, tok.value)
			return nil
		}
	})
	if err != nil {
		return nil, err
	}
	return result, nil
}

func (p *Parser) parseLanguageList() (ir.LanguageList, error) {
	var result ir.LanguageList
	err := p.parseCommaSeparated(tLsquare, tRsquare, func() error {
		if tok, ok := p.consumeToken(tText); !ok {
			return p.failExpectedToken(tText, tok)
		} else if !p.allLanguages.Includes(tok.value) {
			return p.newParseError(tok, "invalid language '%s'; must be one of: %s",
				tok.value, strings.Join(p.allLanguages, ", "))
		} else {
			result = append(result, tok.value)
			return nil
		}
	})
	if err != nil {
		return nil, err
	}
	return result, nil
}

func (p *Parser) parseByteSection() ([]ir.Encoding, error) {
	firstTok := p.peekToken()
	if firstTok.kind == tLsquare {
		if b, err := p.parseByteList(); err == nil {
			return []ir.Encoding{{
				// Default to the old wire format.
				WireFormat: ir.OldWireFormat,
				Bytes:      b,
			}}, nil
		} else {
			return nil, err
		}
	}
	var res []ir.Encoding
	wireFormats := map[ir.WireFormat]struct{}{}
	err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		tok, ok := p.consumeToken(tText)
		if !ok {
			return p.failExpectedToken(tText, tok)
		}
		if teq, ok := p.consumeToken(tEqual); !ok {
			return p.failExpectedToken(tEqual, teq)
		}
		b, err := p.parseByteList()
		if err != nil {
			return err
		}
		wf, err := ir.WireFormatByName(tok.value)
		if err != nil {
			return err
		}
		if _, ok := wireFormats[wf]; ok {
			return p.newParseError(tok, "duplicate wire format: %s", tok.value)
		}
		wireFormats[wf] = struct{}{}
		res = append(res, ir.Encoding{
			WireFormat: wf,
			Bytes:      b,
		})
		return nil
	})
	if err != nil {
		return nil, err
	}
	if len(res) == 0 {
		return nil, p.newParseError(firstTok, "no bytes provided for any wire format")
	}
	return res, nil
}

func (p *Parser) parseByteList() ([]byte, error) {
	var bytes []byte
	err := p.parseCommaSeparated(tLsquare, tRsquare, func() error {
		// Read the byte size.
		tok, ok := p.consumeToken(tText)
		if !ok {
			return p.failExpectedToken(tText, tok)
		}
		if p.peekTokenKind(tText) {
			// First token was the label. Now get the byte size.
			tok, _ = p.consumeToken(tText)
		}

		byteSize, err := strconv.ParseUint(tok.value, 10, 64)
		if err != nil {
			return p.newParseError(tok, "error parsing byte block size: %v", err)
		}
		if byteSize == 0 {
			return p.newParseError(tok, "expected non-zero byte size")
		}

		if tok, ok := p.consumeToken(tColon); !ok {
			return p.failExpectedToken(tColon, tok)
		}

		// Read the type.
		tok, ok = p.consumeToken(tText)
		if !ok {
			return p.failExpectedToken(tText, tok)
		}
		var handler func(byteSize int) ([]byte, error)
		switch tok.value {
		case "raw":
			handler = p.parseByteBlockRaw
		case "num":
			handler = p.parseByteBlockNum
		case "padding":
			handler = p.parseByteBlockPadding
		default:
			return p.newParseError(tok, "unknown byte block type %q", tok.value)
		}
		if b, err := handler(int(byteSize)); err != nil {
			return err
		} else if len(b) != int(byteSize) {
			return p.newParseError(tok, "byte block produced of incorrect size. got %d, want %d", len(b), byteSize)
		} else {
			bytes = append(bytes, b...)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return bytes, nil
}

func (p *Parser) parseByteBlockRaw(byteSize int) ([]byte, error) {
	res := make([]byte, 0, byteSize)
	err := p.parseCommaSeparated(tLparen, tRparen, func() error {
		b, err := p.parseByte()
		if err != nil {
			return err
		}
		res = append(res, b)
		return nil
	})
	return res, err
}

func (p *Parser) parseByteBlockNum(byteSize int) ([]byte, error) {
	if tok, ok := p.consumeToken(tLparen); !ok {
		return nil, p.failExpectedToken(tLparen, tok)
	}
	neg := p.peekTokenKind(tNeg)
	if neg {
		p.consumeToken(tNeg)
	}
	tok, ok := p.consumeToken(tText)
	if !ok {
		return nil, p.failExpectedToken(tText, tok)
	}
	buf := make([]byte, 8)
	uintVal, err := strconv.ParseUint(tok.value, 0, 64)
	if err != nil {
		return nil, p.newParseError(tok, "error parsing unsigned integer num: %v", err)
	}
	if neg {
		intVal := -int64(uintVal)
		if intVal>>(uint(byteSize)*8-1) < -1 {
			return nil, p.newParseError(tok, "num value %d exceeds byte size %d", intVal, byteSize)
		}
		uintVal = uint64(intVal)
	} else {
		if uintVal>>(uint(byteSize)*8) > 0 {
			return nil, p.newParseError(tok, "num value %d exceeds byte size %d", uintVal, byteSize)
		}
	}
	binary.LittleEndian.PutUint64(buf, uintVal)
	if tok, ok := p.consumeToken(tRparen); !ok {
		return nil, p.failExpectedToken(tRparen, tok)
	}
	return buf[:byteSize], nil
}

func (p *Parser) parseByteBlockPadding(byteSize int) ([]byte, error) {
	if !p.peekTokenKind(tLparen) {
		return bytes.Repeat([]byte{0}, byteSize), nil
	}
	if tok, ok := p.consumeToken(tLparen); !ok {
		return nil, p.failExpectedToken(tLparen, tok)
	}
	b, err := p.parseByte()
	if err != nil {
		return nil, err
	}
	if tok, ok := p.consumeToken(tRparen); !ok {
		return nil, p.failExpectedToken(tRparen, tok)
	}
	return bytes.Repeat([]byte{b}, byteSize), nil
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

func (p *Parser) parseCommaSeparated(beginTok, endTok tokenKind, handler func() error) error {
	if tok, ok := p.consumeToken(beginTok); !ok {
		return p.failExpectedToken(beginTok, tok)
	}
	for !p.peekTokenKind(endTok) {
		if err := handler(); err != nil {
			return err
		}
		if !p.peekTokenKind(endTok) {
			if tok, ok := p.consumeToken(tComma); !ok {
				return p.failExpectedToken(tComma, tok)
			}
		}
	}
	if tok, ok := p.consumeToken(endTok); !ok {
		return p.failExpectedToken(endTok, tok)
	}
	return nil
}

func (p *Parser) consumeToken(kind tokenKind) (token, bool) {
	tok := p.nextToken()
	return tok, tok.kind == kind
}

func (p *Parser) peekTokenKind(kind tokenKind) bool {
	return p.peekToken().kind == kind
}

func (p *Parser) peekToken() token {
	if len(p.lookaheads) == 0 {
		tok := p.nextToken()
		p.lookaheads = append(p.lookaheads, tok)
	}
	return p.lookaheads[0]
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

func (p *Parser) failExpectedToken(want tokenKind, got token) error {
	return p.newParseError(got, "unexpected tokenKind: want %q, got %q (value: %q)", want, got.kind, got.value)
}

func (p *Parser) newParseError(tok token, format string, a ...interface{}) error {
	return &parseError{
		input:   p.scanner.Position.Filename,
		line:    tok.line,
		column:  tok.column,
		message: fmt.Sprintf(format, a...),
	}
}
