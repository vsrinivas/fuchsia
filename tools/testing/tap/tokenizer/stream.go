// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tokenizer

import "fmt"

// Iterator iterates over a stream of tokens. This provides the core functionality for a
// TokenStream.
type iterator interface {
	Next() Token
	Peek() Token
	Raw() *rawIterator
}

// NewTokenStream creates a stream of Token values read from input.
func NewTokenStream(input []byte) *TokenStream {
	return &TokenStream{
		iter: &noSpacesIterator{
			raw: &rawIterator{
				stream: Tokenize(input),
			},
		},
	}
}

// TokenStream is a read-only queue of Token values. The next Token in the stream can be
// consumed by calling Next(). The next token can be inspected without being consumed by
// calling Peek(). By default, TokenStream discards whitespace characters as though they
// are not part of the stream. Use Raw get a TokenStream that respects whitespace.
type TokenStream struct {
	iter iterator
}

// Next consumes the next token in the stream.
func (s *TokenStream) Next() Token {
	return s.iter.Next()
}

// Peek returns a read-only copy of the next token in the stream, without consuming it.
func (s *TokenStream) Peek() Token {
	return s.iter.Peek()
}

// Raw returns a TokenStream that includes whitespace characters.
func (s *TokenStream) Raw() *TokenStream {
	return &TokenStream{iter: s.iter.Raw()}
}

// Eat consumes the next token from the stream iff its type matches typ. If the types
// are different, an error is returned.
func (s *TokenStream) Eat(typ TokenType) error {
	token := s.iter.Peek()
	if token.Type != typ {
		return fmt.Errorf("unexpected token: %q", token.Type)
	}
	s.iter.Next()
	return nil
}

// ConcatUntil concatenates the values of the next tokens in this stream as long as their
// types are not anyOf. TypeEOF is implied and need not be specified. Returns the
// concatenated output with outer spaces trimmed.
func (s *TokenStream) ConcatUntil(anyOf ...TokenType) string {
	var values string
	stopAtType := map[TokenType]struct{}{TypeEOF: {}}
	for i := range anyOf {
		stopAtType[anyOf[i]] = struct{}{}
	}
	for {
		if _, ok := stopAtType[s.iter.Peek().Type]; ok {
			break
		}
		values += s.iter.Next().Value
	}
	return values
}
