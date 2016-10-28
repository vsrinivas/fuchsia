// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TokenStream is the interface between the lexer and the parser. The lexer
// creates a TokenStream which the parser consumes.

package lexer

type TokenStream interface {
	// Returns the next Token in the stream without advancing the cursor,
	// or returns the EOF token if the cursor is already past the end.
	PeekNext() Token

	// Advances the cursor in the stream or does nothing if the cursor is
	// already past the end of the stream
	ConsumeNext()
}

// The EOF token is returned by TokenStream to signal the end of the stream.
var eofToken = Token{Kind: EOF}

// EofToken() returns an EOF token.
func EofToken() Token {
	return eofToken
}

// *TokenChan implements TokenStream.
// This implementation uses a non-buffered channel to pass the tokens from the
// lexer to the parser. One end of the channel is held by the lexer and the
// other is in the TokenChan object that is passed to the parser.
type TokenChan struct {
	tokenChan chan Token
	nextToken Token
	// read is true if a token has been read out of the channel into nextToken.
	read bool
}

// See TokenStream.
func (s *TokenChan) PeekNext() (token Token) {
	if !s.read {
		s.read = true
		s.ConsumeNext()
	}

	return s.nextToken
}

// See TokenStream.
func (s *TokenChan) ConsumeNext() {
	if t, open := <-s.tokenChan; open {
		s.nextToken = t
	} else {
		s.nextToken = eofToken
	}
}

// *FilteredTokenStream implements TokenStream
// This implementation uses an underlying implementation of TokenStream, but
// it implements a TokenKind filter. All tokens of the filtered TokenKinds
// will be silently dropped.
type FilteredTokenStream struct {
	// tokenStream is the underlying TokenStream that is the data source.
	tokenStream TokenStream
	// filter contains the TokenKinds that should be skipped in the source
	// TokenStream.
	filter map[TokenKind]bool
	// filteredTokens contains a copy of all tokens that were filtered out.
	filteredTokens []Token
}

// See TokenStream.
func (s *FilteredTokenStream) PeekNext() (token Token) {
	s.skipFiltered()
	return s.tokenStream.PeekNext()
}

// See TokenStream.
func (s *FilteredTokenStream) ConsumeNext() {
	s.skipFiltered()
	s.tokenStream.ConsumeNext()
	s.skipFiltered()
}

func (s *FilteredTokenStream) skipFiltered() {
	t := s.tokenStream.PeekNext()
	for s.isFiltered(t) {
		s.filteredTokens = append(s.filteredTokens, t)
		s.tokenStream.ConsumeNext()
		t = s.tokenStream.PeekNext()
	}
}

// isFiltered checks if a Token is of a filtered TokenKind.
func (s *FilteredTokenStream) isFiltered(token Token) (ok bool) {
	_, ok = s.filter[token.Kind]
	return
}

// FilteredTokens returns the list of tokens that were filtered by the FilteredTokenStream.
func (s *FilteredTokenStream) FilteredTokens() []Token {
	return s.filteredTokens
}

func NewFilteredTokenStream(ts TokenStream, filter []TokenKind) (filteredTS *FilteredTokenStream) {
	filteredTS = &FilteredTokenStream{ts, map[TokenKind]bool{}, []Token{}}
	for _, tk := range filter {
		if tk == EOF {
			// We don't allow filtering EOF since that would cause an infinite loop.
			panic("If the EOF token is filtered, there is no way to find the end of the stream.")
		}
		filteredTS.filter[tk] = true
	}
	return
}
