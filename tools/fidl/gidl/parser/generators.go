// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"bytes"
	"encoding/binary"
	"strconv"
)

type byteGenParser func() ([]byte, error)

func (p *Parser) getByteGenParser(name string) (func() ([]byte, error), bool) {
	// This hardcoded approach should not be extended too much. When we add more
	// generators, we should instead implement implement generic parsing for an
	// invocation grammar like this:
	//
	//     name ( "(" ( param ",")* ( keyword ":" param ",")* ")" )? ":" size
	//
	// The control flow would look something like this:
	//
	//     tokens -> invocation struct -> generator -> []byte
	switch name {
	case "padding":
		return p.parseByteGenPadding, true
	case "repeat":
		return p.parseByteGenRepeat, true
	case "num":
		return p.parseByteGenNum, true
	default:
		return nil, false
	}
}

func (p *Parser) parseColonSize() (uint64, error) {
	if _, err := p.consumeToken(tColon); err != nil {
		return 0, err
	}
	tok, err := p.consumeToken(tText)
	if err != nil {
		return 0, err
	}
	size, err := strconv.ParseUint(tok.value, 10, 64)
	if err != nil {
		return 0, p.newParseError(tok, "error parsing byte size: %v", err)
	}
	if size == 0 {
		return 0, p.newParseError(tok, "expected non-zero byte size")
	}
	return size, nil
}

func (p *Parser) parseByteGenPadding() ([]byte, error) {
	size, err := p.parseColonSize()
	if err != nil {
		return nil, err
	}
	return make([]byte, size), nil
}

func (p *Parser) parseByteGenRepeat() ([]byte, error) {
	if _, err := p.consumeToken(tLparen); err != nil {
		return nil, err
	}
	b, err := p.parseByte()
	if err != nil {
		return nil, err
	}
	if _, err := p.consumeToken(tRparen); err != nil {
		return nil, err
	}
	size, err := p.parseColonSize()
	if err != nil {
		return nil, err
	}
	return bytes.Repeat([]byte{b}, int(size)), nil
}

func (p *Parser) parseByteGenNum() ([]byte, error) {
	if _, err := p.consumeToken(tLparen); err != nil {
		return nil, err
	}
	neg := p.peekTokenKind(tNeg)
	if neg {
		p.nextToken()
	}
	tok, err := p.consumeToken(tText)
	if err != nil {
		return nil, err
	}
	buf := make([]byte, 8)
	uintVal, err := strconv.ParseUint(tok.value, 0, 64)
	if err != nil {
		return nil, p.newParseError(tok, "error parsing num: %v", err)
	}
	if _, err := p.consumeToken(tRparen); err != nil {
		return nil, err
	}
	size, err := p.parseColonSize()
	if err != nil {
		return nil, err
	}
	if neg {
		intVal := -int64(uintVal)
		if intVal>>(uint(size)*8-1) < -1 {
			return nil, p.newParseError(tok, "num %d exceeds byte size %d", intVal, size)
		}
		uintVal = uint64(intVal)
	} else {
		if uintVal>>(uint(size)*8) > 0 {
			return nil, p.newParseError(tok, "num %d exceeds byte size %d", uintVal, size)
		}
	}
	binary.LittleEndian.PutUint64(buf, uintVal)
	return buf[:size], nil
}
