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

	"go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
)

type Parser struct {
	scanner    scanner.Scanner
	lookaheads []token
	config     Config
	// Used to validate that handles are defined and used exactly once.
	handles map[ir.Handle]handleInfo
}

type Config struct {
	// All supported languages, used to validate bindings allowlist/denylist.
	Languages ir.LanguageList
	// All supported wire formats, used to validate `bytes` sections.
	WireFormats ir.WireFormatList
}

type handleInfo struct {
	defined       bool
	usesInValue   int
	usesInHandles int
}

func mergeHandleInfo(a, b handleInfo) handleInfo {
	return handleInfo{
		defined:       a.defined || b.defined,
		usesInValue:   a.usesInValue + b.usesInValue,
		usesInHandles: a.usesInHandles + b.usesInHandles,
	}
}

func NewParser(name string, input io.Reader, config Config) *Parser {
	var p Parser
	p.scanner.Position.Filename = name
	p.scanner.Init(input)
	p.config = config
	// This is reset in parseBody. We need to set it here too so that unit tests
	// for functions like parseHandleDefs don't panic on nil map assignment.
	p.handles = make(map[ir.Handle]handleInfo)
	return &p
}

func (p *Parser) Parse() (ir.All, error) {
	var result ir.All
	for !p.peekTokenKind(tEof) {
		if err := p.parseSection(&result); err != nil {
			return ir.All{}, err
		}
	}
	// TODO(fxbug.dev/8076) Add validation checks for error codes after parsing.
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
	tHash
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
	"#",
}

var (
	isUnitTokenKind = make(map[tokenKind]struct{})
	textToTokenKind = make(map[string]tokenKind)
)

func init() {
	for index, text := range tokenKindStrings {
		if strings.HasPrefix(text, "<") && strings.HasSuffix(text, ">") {
			continue
		}
		kind := tokenKind(index)
		isUnitTokenKind[kind] = struct{}{}
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
	if _, ok := isUnitTokenKind[t.kind]; ok {
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
	isHandles
	isHandleDefs
	isErr
	isBindingsAllowlist
	isBindingsDenylist
	isEnableSendEventBenchmark
	isEnableEchoCallBenchmark
)

func (kind bodyElement) String() string {
	switch kind {
	case isType:
		return "type"
	case isValue:
		return "value"
	case isBytes:
		return "bytes"
	case isHandles:
		return "handles"
	case isHandleDefs:
		return "handle_defs"
	case isErr:
		return "err"
	case isBindingsAllowlist:
		return "bindings_allowlist"
	case isBindingsDenylist:
		return "bindings_denylist"
	case isEnableSendEventBenchmark:
		return "enable_send_event_benchmark"
	case isEnableEchoCallBenchmark:
		return "enable_echo_call_benchmark"
	default:
		panic("unsupported kind")
	}
}

type body struct {
	Type                     string
	Value                    ir.Value
	Encodings                []ir.Encoding
	HandleDefs               []ir.HandleDef
	Err                      ir.ErrorCode
	BindingsAllowlist        *ir.LanguageList
	BindingsDenylist         *ir.LanguageList
	EnableSendEventBenchmark bool
	EnableEchoCallBenchmark  bool
}

func (b *body) addEncoding(e ir.Encoding) {
	for i := range b.Encodings {
		if b.Encodings[i].WireFormat == e.WireFormat {
			if b.Encodings[i].Bytes == nil {
				b.Encodings[i].Bytes = e.Bytes
			}
			if b.Encodings[i].Handles == nil {
				b.Encodings[i].Handles = e.Handles
			}
			return
		}
	}
	b.Encodings = append(b.Encodings, e)
}

type sectionMetadata struct {
	requiredKinds map[bodyElement]struct{}
	optionalKinds map[bodyElement]struct{}
	setter        func(name string, body body, all *ir.All)
}

var sections = map[string]sectionMetadata{
	"success": {
		requiredKinds: map[bodyElement]struct{}{isValue: {}, isBytes: {}},
		optionalKinds: map[bodyElement]struct{}{
			isHandles: {}, isHandleDefs: {}, isBindingsAllowlist: {}, isBindingsDenylist: {},
		},
		setter: func(name string, body body, all *ir.All) {
			encodeSuccess := ir.EncodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				HandleDefs:        body.HandleDefs,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.EncodeSuccess = append(all.EncodeSuccess, encodeSuccess)
			decodeSuccess := ir.DecodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				HandleDefs:        body.HandleDefs,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.DecodeSuccess = append(all.DecodeSuccess, decodeSuccess)
		},
	},
	"encode_success": {
		requiredKinds: map[bodyElement]struct{}{isValue: {}, isBytes: {}},
		optionalKinds: map[bodyElement]struct{}{
			isHandles: {}, isHandleDefs: {}, isBindingsAllowlist: {}, isBindingsDenylist: {},
		},
		setter: func(name string, body body, all *ir.All) {
			result := ir.EncodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				HandleDefs:        body.HandleDefs,
				BindingsAllowlist: body.BindingsAllowlist,
			}
			all.EncodeSuccess = append(all.EncodeSuccess, result)
		},
	},
	"decode_success": {
		requiredKinds: map[bodyElement]struct{}{isValue: {}, isBytes: {}},
		optionalKinds: map[bodyElement]struct{}{
			isHandles: {}, isHandleDefs: {}, isBindingsAllowlist: {}, isBindingsDenylist: {},
		},
		setter: func(name string, body body, all *ir.All) {
			result := ir.DecodeSuccess{
				Name:              name,
				Value:             body.Value,
				Encodings:         body.Encodings,
				HandleDefs:        body.HandleDefs,
				BindingsAllowlist: body.BindingsAllowlist,
			}
			all.DecodeSuccess = append(all.DecodeSuccess, result)
		},
	},
	"encode_failure": {
		requiredKinds: map[bodyElement]struct{}{isValue: {}, isErr: {}},
		optionalKinds: map[bodyElement]struct{}{
			isHandleDefs: {}, isBindingsAllowlist: {}, isBindingsDenylist: {},
		},
		setter: func(name string, body body, all *ir.All) {
			result := ir.EncodeFailure{
				Name:       name,
				Value:      body.Value,
				HandleDefs: body.HandleDefs,
				// TODO(fxb/52495): Temporarily hardcoded to v1. We should
				// either make encode_failure tests specify a wireformat -> err
				// mapping, or remove the WireFormats field.
				WireFormats:       []ir.WireFormat{ir.V1WireFormat},
				Err:               body.Err,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.EncodeFailure = append(all.EncodeFailure, result)
		},
	},
	"decode_failure": {
		requiredKinds: map[bodyElement]struct{}{isType: {}, isBytes: {}, isErr: {}},
		optionalKinds: map[bodyElement]struct{}{
			isHandles: {}, isHandleDefs: {}, isBindingsAllowlist: {}, isBindingsDenylist: {},
		},
		setter: func(name string, body body, all *ir.All) {
			result := ir.DecodeFailure{
				Name:              name,
				Type:              body.Type,
				Encodings:         body.Encodings,
				HandleDefs:        body.HandleDefs,
				Err:               body.Err,
				BindingsAllowlist: body.BindingsAllowlist,
				BindingsDenylist:  body.BindingsDenylist,
			}
			all.DecodeFailure = append(all.DecodeFailure, result)
		},
	},
	"benchmark": {
		requiredKinds: map[bodyElement]struct{}{isValue: {}},
		optionalKinds: map[bodyElement]struct{}{
			isHandleDefs: {}, isBindingsAllowlist: {}, isBindingsDenylist: {},
			isEnableSendEventBenchmark: {}, isEnableEchoCallBenchmark: {},
		},
		setter: func(name string, body body, all *ir.All) {
			benchmark := ir.Benchmark{
				Name:                     name,
				Value:                    body.Value,
				HandleDefs:               body.HandleDefs,
				BindingsAllowlist:        body.BindingsAllowlist,
				BindingsDenylist:         body.BindingsDenylist,
				EnableSendEventBenchmark: body.EnableSendEventBenchmark,
				EnableEchoCallBenchmark:  body.EnableEchoCallBenchmark,
			}
			all.Benchmark = append(all.Benchmark, benchmark)
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
	tok, err := p.consumeToken(tText)
	if err != nil {
		return sectionMetadata{}, "", err
	}

	section, ok := sections[tok.value]
	if !ok {
		return sectionMetadata{}, "", p.newParseError(tok, "unknown section %s", tok.value)
	}

	tok, err = p.consumeToken(tLparen)
	if err != nil {
		return sectionMetadata{}, "", err
	}

	tok, err = p.consumeToken(tString)
	if err != nil {
		return sectionMetadata{}, "", err
	}
	name := tok.value

	tok, err = p.consumeToken(tRparen)
	if err != nil {
		return sectionMetadata{}, "", err
	}

	return section, name, nil
}

func (p *Parser) parseBody(requiredKinds map[bodyElement]struct{}, optionalKinds map[bodyElement]struct{}) (body, error) {
	var (
		result      body
		parsedKinds = make(map[bodyElement]struct{})
	)
	bodyTok, err := p.peekToken()
	if err != nil {
		return result, err
	}
	p.handles = make(map[ir.Handle]handleInfo)
	if err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		return p.parseSingleBodyElement(&result, parsedKinds)
	}); err != nil {
		return result, err
	}
	for requiredKind := range requiredKinds {
		if _, ok := parsedKinds[requiredKind]; !ok {
			return result, p.newParseError(bodyTok, "missing required parameter '%s'", requiredKind)
		}
	}
	for parsedKind := range parsedKinds {
		_, requiredKind := requiredKinds[parsedKind]
		_, optionalKind := optionalKinds[parsedKind]
		if !requiredKind && !optionalKind {
			return result, p.newParseError(bodyTok, "parameter '%s' does not apply to element", parsedKind)
		}
	}
	for h, info := range p.handles {
		if !info.defined {
			return result, p.newParseError(bodyTok, "missing definition for handle #%d", h)
		}
		if info.usesInValue > 1 {
			return result, p.newParseError(bodyTok, "handle #%d used more than once in 'value' section", h)
		}
		if info.usesInHandles > 1 {
			return result, p.newParseError(bodyTok, "handle #%d used more than once in 'handles' section", h)
		}
		if info.usesInValue == 0 && info.usesInHandles == 0 {
			return result, p.newParseError(bodyTok, "unused handle #%d", h)
		}
	}
	p.handles = nil
	return result, nil
}

func (p *Parser) parseSingleBodyElement(result *body, all map[bodyElement]struct{}) error {
	tok, err := p.consumeToken(tText)
	if err != nil {
		return err
	}
	if _, err := p.consumeToken(tEqual); err != nil {
		return err
	}
	var kind bodyElement
	switch tok.value {
	case "type":
		tok, err := p.consumeToken(tText)
		if err != nil {
			return err
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
		for _, e := range encodings {
			result.addEncoding(e)
		}
		kind = isBytes
	case "handles":
		encodings, err := p.parseHandleSection()
		if err != nil {
			return err
		}
		for _, e := range encodings {
			result.addEncoding(e)
		}
		kind = isHandles
	case "handle_defs":
		handleDefs, err := p.parseHandleDefSection()
		if err != nil {
			return err
		}
		result.HandleDefs = handleDefs
		kind = isHandleDefs
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
	case "enable_send_event_benchmark":
		value, err := p.parseValue()
		if err != nil {
			return err
		}
		boolValue, ok := value.(bool)
		if !ok {
			return p.newParseError(tok, "expected boolean value")
		}
		result.EnableSendEventBenchmark = boolValue
		kind = isEnableSendEventBenchmark
	case "enable_echo_call_benchmark":
		value, err := p.parseValue()
		if err != nil {
			return err
		}
		boolValue, ok := value.(bool)
		if !ok {
			return p.newParseError(tok, "expected boolean value")
		}
		result.EnableEchoCallBenchmark = boolValue
		kind = isEnableEchoCallBenchmark
	default:
		return p.newParseError(tok, "must be type, value, bytes, err, bindings_allowlist or bindings_denylist")
	}
	if kind == 0 {
		panic("kind must be set")
	}
	if _, ok := all[kind]; ok {
		return p.newParseError(tok, "duplicate %s found", kind)
	}
	all[kind] = struct{}{}
	return nil
}

func (p *Parser) parseValue() (interface{}, error) {
	tok, err := p.peekToken()
	if err != nil {
		return nil, err
	}
	switch tok.kind {
	case tText:
		tok, err := p.nextToken()
		if err != nil {
			return nil, err
		}
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
		return p.parseRecord(tok.value)
	case tLsquare:
		return p.parseSlice()
	case tString:
		tok, err := p.nextToken()
		if err != nil {
			return nil, err
		}
		return tok.value, nil
	case tNeg:
		if _, err := p.nextToken(); err != nil {
			return nil, err
		}
		if tok, err := p.consumeToken(tText); err != nil {
			return nil, err
		} else {
			return parseNum(tok, true)
		}
	case tHash:
		return p.parseHandle(handleInfo{usesInValue: 1})
	default:
		tok, err := p.peekToken()
		if err != nil {
			return nil, err
		}
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

func (p *Parser) parseRecord(name string) (interface{}, error) {
	obj := ir.Record{Name: name}
	err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		tokFieldName, err := p.consumeToken(tText)
		if err != nil {
			return err
		}
		key := decodeFieldKey(tokFieldName.value)
		if _, err := p.consumeToken(tColon); err != nil {
			return err
		}
		var val interface{}
		if key.IsKnown() {
			val, err = p.parseValue()
		} else {
			val, err = p.parseUnknownData()
		}
		if err != nil {
			return err
		}
		obj.Fields = append(obj.Fields, ir.Field{Key: key, Value: val})
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
		return ir.FieldKey{UnknownOrdinal: uint64(ord)}
	}
	return ir.FieldKey{Name: field}
}

// This is not expressed in terms of parseBody because it parses bytes/handles
// without wire formats (e.g. `bytes = [...]`, not `bytes = { v1 = [...] }`).
func (p *Parser) parseUnknownData() (ir.UnknownData, error) {
	var result ir.UnknownData
	parsedKinds := make(map[bodyElement]struct{})
	bodyTok, err := p.peekToken()
	if err != nil {
		return result, err
	}
	if err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		tok, err := p.consumeToken(tText)
		if err != nil {
			return err
		}
		if _, err := p.consumeToken(tEqual); err != nil {
			return err
		}
		var kind bodyElement
		switch tok.value {
		case "bytes":
			bytes, err := p.parseByteList()
			if err != nil {
				return err
			}
			result.Bytes = bytes
			kind = isBytes
		case "handles":
			handles, err := p.parseHandleList(handleInfo{usesInValue: 1})
			if err != nil {
				return err
			}
			result.Handles = handles
			kind = isHandles
		default:
			return p.newParseError(tok, "parameter '%s' does not apply to unknown data, must be bytes", tok)
		}
		if kind == 0 {
			panic("kind must be set")
		}
		if _, ok := parsedKinds[kind]; ok {
			return p.newParseError(tok, "duplicate parameter '%s' found", kind)
		}
		parsedKinds[kind] = struct{}{}
		return nil
	}); err != nil {
		return result, err
	}
	for _, requiredKind := range []bodyElement{isBytes} {
		if _, ok := parsedKinds[requiredKind]; !ok {
			return result, p.newParseError(bodyTok, "missing required parameter '%s'", requiredKind)
		}
	}
	return result, nil
}

func (p *Parser) parseErrorCode() (ir.ErrorCode, error) {
	tok, err := p.consumeToken(tText)
	if err != nil {
		return "", err
	}
	code := ir.ErrorCode(tok.value)
	if _, ok := ir.AllErrorCodes[code]; !ok {
		return "", p.newParseError(tok, "unknown error code: %s", tok.value)
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
		if tok, err := p.consumeToken(tText); err != nil {
			return err
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
		if tok, err := p.consumeToken(tText); err != nil {
			return err
		} else if !p.config.Languages.Includes(tok.value) {
			return p.newParseError(tok, "invalid language '%s'; must be one of: %s",
				tok.value, strings.Join(p.config.Languages, ", "))
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
	var res []ir.Encoding
	firstTok, err := p.peekToken()
	if err != nil {
		return nil, err
	}
	err = p.parseWireFormatMapping(func(wireFormats []ir.WireFormat) error {
		b, err := p.parseByteList()
		if err != nil {
			return err
		}
		for _, wf := range wireFormats {
			res = append(res, ir.Encoding{
				WireFormat: wf,
				Bytes:      b,
			})
		}
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
		tok, err := p.peekToken()
		if err != nil {
			return err
		}
		if tok.kind == tText && 'a' <= tok.value[0] && tok.value[0] <= 'z' {
			p.nextToken()
			parse, ok := p.getByteGenParser(tok.value)
			if !ok {
				return p.newParseError(tok, "invalid byte syntax: %s", tok.value)
			}
			b, err := parse()
			if err != nil {
				return err
			}
			bytes = append(bytes, b...)
		} else {
			b, err := p.parseByte()
			if err != nil {
				return err
			}
			bytes = append(bytes, b)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return bytes, nil
}

func (p *Parser) parseByte() (byte, error) {
	tok, err := p.consumeToken(tText)
	if err != nil {
		return 0, err
	}
	if len(tok.value) == 3 && tok.value[0] == '\'' && tok.value[2] == '\'' {
		return tok.value[1], nil
	}
	b, err := strconv.ParseUint(tok.value, 0, 8)
	if err != nil {
		return 0, p.newParseError(tok, "invalid byte syntax: %s", tok.value)
	}
	return byte(b), nil
}

func (p *Parser) parseHandleSection() ([]ir.Encoding, error) {
	var res []ir.Encoding
	err := p.parseWireFormatMapping(func(wireFormats []ir.WireFormat) error {
		h, err := p.parseHandleList(handleInfo{usesInHandles: 1})
		if err != nil {
			return err
		}
		for _, wf := range wireFormats {
			res = append(res, ir.Encoding{
				WireFormat: wf,
				Handles:    h,
			})
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return res, nil
}

func (p *Parser) parseHandleList(info handleInfo) ([]ir.Handle, error) {
	var handles []ir.Handle
	err := p.parseCommaSeparated(tLsquare, tRsquare, func() error {
		h, err := p.parseHandle(info)
		if err != nil {
			return err
		}
		handles = append(handles, h)
		return nil
	})
	if err != nil {
		return nil, err
	}
	return handles, nil
}

func (p *Parser) parseHandle(info handleInfo) (ir.Handle, error) {
	tok, err := p.consumeToken(tHash)
	if err != nil {
		return 0, err
	}
	tok, err = p.consumeToken(tText)
	if err != nil {
		return 0, err
	}
	index, err := strconv.ParseInt(tok.value, 10, 0)
	if err != nil {
		return 0, p.newParseError(tok, "invalid handle syntax: %s", tok.value)
	}
	if index < 0 {
		panic("impossible because tok is tText not tNeg")
	}
	h := ir.Handle(index)
	p.handles[h] = mergeHandleInfo(p.handles[h], info)
	return h, nil
}

func (p *Parser) parseHandleDefSection() ([]ir.HandleDef, error) {
	var res []ir.HandleDef
	expected := ir.Handle(0)
	err := p.parseCommaSeparated(tLacco, tRacco, func() error {
		tok, err := p.peekToken()
		if err != nil {
			return err
		}
		h, err := p.parseHandle(handleInfo{defined: true})
		if err != nil {
			return err
		}
		if h != expected {
			return p.newParseError(
				tok, "want #%d, got #%d (handle_defs must be #0, #1, #2, etc.)", expected, h)
		}
		expected++
		tok, err = p.consumeToken(tEqual)
		if err != nil {
			return err
		}
		hd, err := p.parseHandleDef()
		if err != nil {
			return err
		}
		res = append(res, hd)
		return nil
	})
	if err != nil {
		return nil, err
	}
	return res, nil
}

func (p *Parser) parseHandleDef() (ir.HandleDef, error) {
	tok, err := p.consumeToken(tText)
	if err != nil {
		return ir.HandleDef{}, err
	}
	subtype, ok := ir.HandleSubtypeByName(tok.value)
	if !ok {
		return ir.HandleDef{}, p.newParseError(tok, "invalid handle subtype: %s", tok.value)
	}
	tok, err = p.consumeToken(tLparen)
	if err != nil {
		return ir.HandleDef{}, err
	}
	tok, err = p.consumeToken(tRparen)
	if err != nil {
		return ir.HandleDef{}, err
	}
	return ir.HandleDef{Subtype: subtype}, nil
}

func (p *Parser) parseWireFormatMapping(handler func([]ir.WireFormat) error) error {
	seenWireFormats := map[ir.WireFormat]struct{}{}
	return p.parseCommaSeparated(tLacco, tRacco, func() error {
		var wireFormats []ir.WireFormat
		for {
			tok, err := p.consumeToken(tText)
			if err != nil {
				return err
			}
			wf := ir.WireFormat(tok.value)
			if !p.config.WireFormats.Includes(wf) {
				return p.newParseError(tok, "invalid wire format '%s'; must be one of: %s",
					tok.value, p.config.WireFormats.Join(", "))
			}
			if _, ok := seenWireFormats[wf]; ok {
				return p.newParseError(tok, "duplicate wire format: %s", tok.value)
			}
			seenWireFormats[wf] = struct{}{}
			wireFormats = append(wireFormats, wf)
			if p.peekTokenKind(tEqual) {
				break
			}
			if _, err := p.consumeToken(tComma); err != nil {
				return err
			}
		}
		if _, err := p.consumeToken(tEqual); err != nil {
			return err
		}
		return handler(wireFormats)
	})
}

func (p *Parser) parseCommaSeparated(beginTok, endTok tokenKind, handler func() error) error {
	if _, err := p.consumeToken(beginTok); err != nil {
		return err
	}
	for !p.peekTokenKind(endTok) {
		if err := handler(); err != nil {
			return err
		}
		if !p.peekTokenKind(endTok) {
			if _, err := p.consumeToken(tComma); err != nil {
				return err
			}
		}
	}
	if _, err := p.consumeToken(endTok); err != nil {
		return err
	}
	return nil
}

func (p *Parser) consumeToken(kind tokenKind) (token, error) {
	tok, err := p.nextToken()
	if err != nil {
		return token{}, err
	} else if tok.kind != kind {
		return token{}, p.newParseError(tok, "unexpected tokenKind: want %q, got %q (value: %q)", kind, tok.kind, tok.value)
	}
	return tok, nil
}

func (p *Parser) peekTokenKind(kind tokenKind) bool {
	tok, err := p.peekToken()
	if err != nil {
		return false
	}
	return tok.kind == kind
}

func (p *Parser) peekToken() (token, error) {
	if len(p.lookaheads) == 0 {
		tok, err := p.nextToken()
		if err != nil {
			return token{}, err
		}
		p.lookaheads = append(p.lookaheads, tok)
	}
	return p.lookaheads[0], nil
}

func (p *Parser) nextToken() (token, error) {
	if len(p.lookaheads) != 0 {
		var tok token
		tok, p.lookaheads = p.lookaheads[0], p.lookaheads[1:]
		return tok, nil
	}
	return p.scanToken()
}

func (p *Parser) scanToken() (token, error) {
	// eof
	if tok := p.scanner.Scan(); tok == scanner.EOF {
		return token{tEof, "", 0, 0}, nil
	}
	pos := p.scanner.Position

	// unit tokens
	text := p.scanner.TokenText()
	if kind, ok := textToTokenKind[text]; ok {
		return token{kind, text, pos.Line, pos.Column}, nil
	}

	// string
	if text[0] == '"' {
		tok := token{tString, "", pos.Line, pos.Column}
		s, err := strconv.Unquote(text)
		if err != nil {
			return tok, p.newParseError(tok, "improperly escaped string, %s: %s", err, text)
		}
		tok.value = s
		return tok, nil
	}

	// text
	return token{tText, text, pos.Line, pos.Column}, nil
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

func (p *Parser) newParseError(tok token, format string, a ...interface{}) error {
	return &parseError{
		input:   p.scanner.Position.Filename,
		line:    tok.line,
		column:  tok.column,
		message: fmt.Sprintf(format, a...),
	}
}
