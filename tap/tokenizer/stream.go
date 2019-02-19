// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tokenizer

// NewStream creates a stream of Token values read from input.
func NewTokenStream(input []byte) *TokenStream {
	return &TokenStream{
		raw: &RawTokenStream{
			stream: Tokenize(input),
		},
	}
}

// TokenStream is a read-only queue of Token values. The next Token in the stream can be
// consumed by calling Next(). The next token can be observed without being consumed by
// calling Peek(). By default, TokenStream discards \s characters as though they are not
// part of the stream. They are discarded when calling both Peek and Next.
type TokenStream struct {
	raw *RawTokenStream
}

// Next consumes the next token in the stream. Space characters are skipped.
func (s *TokenStream) Next() Token {
	for {
		next := s.raw.Next()
		if next.Type != TypeSpace {
			return next
		}
	}
}

// Peek returns a read-only copy of the next token in the stream, without consuming it.
// Space characters are skipped.
func (s *TokenStream) Peek() Token {
	for {
		next := s.raw.Peek()
		if next.Type == TypeSpace {
			s.raw.Next()
			continue
		}
		return next
	}
}

// Raw returns a RawTokenStream using the same underlying stream of Tokens as this
// TokenStream.
func (s TokenStream) Raw() *RawTokenStream {
	return s.raw
}

// RawTokenStream is a read-only queue of Token values. The next Token in the stream can
// be consumed by calling Next(). The next token can be observed without being consumed
// by calling Peek().
type RawTokenStream struct {
	stream    <-chan Token
	eof       bool
	lookahead *Token
}

// Next consumes the next token in the stream.
func (s *RawTokenStream) Next() Token {
	if s.eof {
		return EOFToken()
	}

	next := new(Token)
	if s.lookahead == nil {
		*next = <-s.stream
	} else {
		next = s.lookahead
		s.lookahead = nil
	}

	if next.Type == TypeEOF {
		s.eof = true
	}

	return *next
}

// Peek returns a read-only copy of the next token in the stream, without consuming it.
func (s *RawTokenStream) Peek() Token {
	if s.eof {
		return EOFToken()
	}

	if s.lookahead == nil {
		s.lookahead = new(Token)
		*s.lookahead = <-s.stream
	}

	return *s.lookahead
}
