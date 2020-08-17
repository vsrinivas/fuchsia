// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tokenizer

import (
	"log"
	"strconv"
	"unicode"
	"unicode/utf8"
)

// TokenType describes the category a Token belongs to.
type TokenType string

// TokenType values recognized by the lexer.
const (
	TypePound   TokenType = "POUND"   // '#'
	TypeNumber  TokenType = "NUMBER"  // A number
	TypeText    TokenType = "TEXT"    // Catch-all type
	TypeDot     TokenType = "DOT"     // '.'
	TypeNewline TokenType = "NEWLINE" // '\n'
	TypeEOF     TokenType = "EOF"     // Pseudo token to signal the end of input.
	TypeSpace   TokenType = "SPACE"   // A whitespace character
	TypeDash    TokenType = "DASH"    // '-'
)

// Token represents some atomic TAP output string.
type Token struct {
	Type  TokenType
	Value string
}

// Tokenize generates a channel of Tokens read from the given input.
func Tokenize(input []byte) <-chan Token {
	l := &lexer{
		input:  input,
		Tokens: make(chan Token, 1),
	}
	go l.run()
	return l.Tokens
}

// EOFToken is emitted to signal the end of input.
func EOFToken() Token {
	return Token{
		Type:  TypeEOF,
		Value: "",
	}
}

// The rune emitted when the end of input has been reached.
const eof = rune(-1)

// State represents a lexical analysis state. Each state accepts a lexer as input and
// returns the next lexer state. If the output state is nil, lexing stops.
type state func(*lexer) state

// Lexer manages the position of a lexical analysis on some TAP output string.
type lexer struct {
	input  []byte
	start  int
	pos    int
	width  int
	Tokens chan Token
}

func (l *lexer) run() {
	for state := lexAny; state != nil; {
		state = state(l)
	}
	close(l.Tokens)
}

func (l *lexer) emit(t TokenType) {
	l.Tokens <- Token{Type: t, Value: string(l.input[l.start:l.pos])}
	l.start = l.pos
}

func (l *lexer) next() rune {
	if l.pos >= len(l.input) {
		l.width = 0
		return eof
	}

	// Read the next rune, skipping over all invalid utf8 sequences.
	var rn rune
	rn, l.width = utf8.DecodeRune(l.input[l.pos:])
	for rn == utf8.RuneError && l.pos < len(l.input) {
		log.Printf("invalid UTF-8 found at pos %d:\n\n%s", l.pos, string(l.input))
		l.pos++
		rn, l.width = utf8.DecodeRune(l.input[l.pos:])
	}
	l.pos += l.width
	return rn
}

// Returns the current lexeme.
func (l *lexer) lexeme() lexeme {
	if l.pos >= len(l.input) {
		return lexeme(eof)
	}
	return lexeme(l.input[l.pos : l.pos+1][0])
}

// LexAny is the lexer start state. Its job is to put the lexer into the proper state
// according to the next input rune. Other states should return to this state after
// emitting their lexemes. They should also not consume runes using l.next() immediately
// before entering this state.
func lexAny(l *lexer) state {
	lxm := l.lexeme()
	if lxm.isEOF() {
		l.emit(TypeEOF)
		return nil
	}

	l.start = l.pos

	switch {
	case lxm.isDash():
		l.next()
		l.emit(TypeDash)
		return lexAny
	case lxm.isNewline():
		l.next()
		l.emit(TypeNewline)
		return lexAny
	case lxm.isDot():
		l.next()
		l.emit(TypeDot)
		return lexAny
	case lxm.isPound():
		l.next()
		l.emit(TypePound)
		return lexAny
	case lxm.isSpace():
		return lexSpace
	case lxm.isDigit():
		return lexNumber
	}

	return lexText
}

func lexSpace(l *lexer) state {
	return lexUntil(l, TypeSpace, func(lxm lexeme) bool { return !lxm.isSpace() })
}

func lexNumber(l *lexer) state {
	return lexUntil(l, TypeNumber, func(lxm lexeme) bool { return !lxm.isDigit() })
}

func lexText(l *lexer) state {
	return lexUntil(l, TypeText, func(lxm lexeme) bool { return lxm.isNonText() })
}

// LexUntil consumes all runes into a token of the given type while `stop` is false.
// Returns lexAny when complete or nil if the end of input was reached.
func lexUntil(l *lexer, typ TokenType, stop func(lexeme) bool) state {
	for {
		lxm := l.lexeme()
		if lxm.isEOF() || stop(lxm) {
			l.emit(typ)
			return lexAny
		}
		if l.next() == eof {
			break
		}
	}
	// Reached EOF
	if l.pos > l.start {
		l.emit(typ)
	}
	l.emit(TypeEOF)
	return nil
}

type lexeme rune

func (l lexeme) isSpace() bool {
	return l != '\n' && unicode.IsSpace(rune(l))
}

func (l lexeme) isNewline() bool {
	return l == '\n'
}

func (l lexeme) isDigit() bool {
	_, err := strconv.Atoi(string(l))
	return err == nil
}

func (l lexeme) isDot() bool {
	return l == '.'
}

func (l lexeme) isDash() bool {
	return l == '-'
}

func (l lexeme) isPound() bool {
	return l == '#'
}

func (l lexeme) isEOF() bool {
	return rune(l) == eof
}

func (l lexeme) isNonText() bool {
	return l.isEOF() || l.isSpace() || l.isNewline() || l.isDigit() || l.isDot() || l.isPound() || l.isDash()
}
