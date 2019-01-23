// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tokenizer

// NewStream creates a stream of Token values read from input.
func NewTokenStream(input []byte) *TokenStream {
	return &TokenStream{
		stream: Tokenize(input),
	}
}

// TokenStream is a read-only queue of Token values.  The next Token in the stream can be
// consumed by calling Next().  The next token can be observed without being consumed by
// calling Peek().
type TokenStream struct {
	eof       bool
	lookahead *Token
	stream    <-chan Token
}

// Next consumes the next token in the stream.
func (s *TokenStream) Next() Token {
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
func (s *TokenStream) Peek() Token {
	if s.eof {
		return EOFToken()
	}

	if s.lookahead == nil {
		s.lookahead = new(Token)
		*s.lookahead = <-s.stream
	}

	return *s.lookahead
}
