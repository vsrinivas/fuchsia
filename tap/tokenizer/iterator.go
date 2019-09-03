// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tokenizer

// noSpacesIterator iterates over a stream of tokens and skips whitespace characters, with
// the exception of newlines.
type noSpacesIterator struct {
	raw *rawIterator
}

// Next consumes the next token in the stream.
func (s *noSpacesIterator) Next() Token {
	for {
		next := s.raw.Next()
		if next.Type != TypeSpace {
			return next
		}
	}
}

// Peek returns a read-only copy of the next token in the stream, without consuming it.
func (s *noSpacesIterator) Peek() Token {
	for {
		next := s.raw.Peek()
		if next.Type == TypeSpace {
			s.raw.Next()
			continue
		}
		return next
	}
}

// Raw returns a rawIterator using the same underlying channel of Tokens.
func (s noSpacesIterator) Raw() *rawIterator {
	return s.raw
}

// rawIterator iterates over a stream of tokens, including whitespace characters.
type rawIterator struct {
	stream    <-chan Token
	eof       bool
	lookahead *Token
}

// Next consumes the next token in the stream.
func (s *rawIterator) Next() Token {
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
func (s *rawIterator) Peek() Token {
	if s.eof {
		return EOFToken()
	}

	if s.lookahead == nil {
		s.lookahead = new(Token)
		*s.lookahead = <-s.stream
	}

	return *s.lookahead
}

// Raw returns a rawIterator using the same underlying channel of Tokens.
func (s rawIterator) Raw() *rawIterator {
	return &s
}
