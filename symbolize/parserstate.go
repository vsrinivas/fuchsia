// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"reflect"
	"strconv"
	"strings"
)

type ParserState string
type Parser func(*ParserState) interface{}

func (b *ParserState) peek(what string) bool {
	if len(*b) < len(what) {
		return false
	}
	return (*b)[:len(what)] == ParserState(what)
}

func (b *ParserState) expect(what string) bool {
	if b.peek(what) {
		*b = (*b)[len(what):]
		return true
	}
	return false
}

func (b *ParserState) before(what string) (string, error) {
	idx := strings.Index(string(*b), what)
	if idx == -1 {
		return "", fmt.Errorf("expected '%s'", what)
	}
	str := (*b)[:idx]
	*b = (*b)[idx+len(what):]
	return string(str), nil
}

func (b *ParserState) decBefore(what string) (uint64, error) {
	out, err := b.before(what)
	if err != nil {
		return 0, err
	}
	return strconv.ParseUint(out, 10, 64)
}

func (b *ParserState) intBefore(what string) (uint64, error) {
	out, err := b.before(what)
	if err != nil {
		return 0, err
	}
	return strconv.ParseUint(out, 0, 64)
}

func (b *ParserState) floatBefore(what string) (float64, error) {
	out, err := b.before(what)
	if err != nil {
		return 0.0, err
	}
	return strconv.ParseFloat(out, 64)
}

func (b *ParserState) try(p Parser) interface{} {
	save := *b
	out := p(b)
	if out == nil {
		*b = save
	}
	return out
}

func (b *ParserState) prefix(prefix string, p Parser) interface{} {
	return b.try(func(b *ParserState) interface{} {
		if b.expect(prefix) {
			return p(b)
		}
		return nil
	})
}

func (b *ParserState) choice(toTry ...Parser) interface{} {
	for _, parser := range toTry {
		if node := parser(b); node != nil {
			return node
		}
	}
	return nil
}

func (b *ParserState) many(out interface{}, p Parser) {
	iout := reflect.ValueOf(out).Elem()
	for n := p(b); n != nil; n = p(b) {
		iout.Set(reflect.Append(iout, reflect.ValueOf(n)))
	}
}

func (b *ParserState) whitespace() {
	*b = ParserState(strings.TrimLeft(string(*b), " \t\n\r\v\f"))
}
