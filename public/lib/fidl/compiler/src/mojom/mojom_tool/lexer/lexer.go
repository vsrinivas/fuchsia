// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// To use the lexer, call Tokenize with the source string to obtain
// a TokenStream. The lexer is run concurrently so you should be able to
// use the TokenStream before the lexer is done with the source.
//
// The lexer is implemented as a state machine. The states are represented
// by functions (the stateFn type) which accept a lexer and return the
// new state.
//
// Most states also have an isFooStart function which helps determine if
// a transition to Foo is appropriate. Those functions accept a single
// rune as a parameter and return true if the state machine should
// transition to state Foo. Some states do not have such functions on
// account of the transition condition being trivial.
//
// The lexer implementation was inspired by
// http://cuddle.googlecode.com/hg/talk/lex.html

package lexer

import (
	"unicode/utf8"
)

// Tokenize accepts a source string and parses it into a stream of tokens which
// can be read from the returned TokenStream. Comment tokens are ommitted from
// the returned stream.
func Tokenize(source string) TokenStream {
	return NewFilteredTokenStream(
		tokenizeUnfiltered(source),
		[]TokenKind{SingleLineComment, MultiLineComment, EmptyLine})
}

// tokenizeUnfiltered returns a TokenStream which does not filter out any of the
// tokens in the channel. It is used for testing and by Tokenize which adds a
// filter on top of the stream returned by tokenizeUnfiltered.
func tokenizeUnfiltered(source string) TokenStream {
	tokens := make(chan Token)
	l := lexer{source: source, tokens: tokens}
	go l.run()
	return &TokenChan{tokenChan: tokens}
}

type lexer struct {
	// source is the source code to be lexed.
	source string

	// tokens is a channel to which the found tokens are emitted.
	tokens chan Token

	// sourcePosBytes is the number of bytes that have been consumed.
	sourcePosBytes int

	// sourcePos is the number of runes that have been consumed.
	sourcePos int

	// lineno is the current line number.
	lineNo int

	// linePos is how many runes have been consumed since the beginning of the
	// line.
	linePos int

	// linePosBytes is how many bytes have been consumed since the beginning of
	// the line.
	linePosBytes int

	// curTokenSourcePosBytes is the number of bytes consumed prior to the
	// beginning of the current token.
	curTokenSourcePosBytes int

	// curTokenSourcePos is the number of runes consumed prior to the beginning of
	// the current token.
	curTokenSourcePos int

	// curTokenLineNo is the line number on which the current token begins.
	curTokenLineNo int

	// curTokenLinePos is the number of runes since the beginning of the line
	// where the current token begins.
	curTokenLinePos int

	// curTokenLinePosBytes is the number of bytes since the beginning of the line
	// where the current token begins.
	curTokenLinePosBytes int
}

// CurText returns the consumed part of the current token.
func (l *lexer) CurText() string {
	return l.source[l.curTokenSourcePosBytes:l.sourcePosBytes]
}

// emitToken emits the current token and begins a new token.
func (l *lexer) emitToken(tokenType TokenKind) {
	l.tokens <- Token{
		Kind:           tokenType,
		Text:           l.source[l.curTokenSourcePosBytes:l.sourcePosBytes],
		SourcePos:      l.curTokenSourcePos,
		LineNo:         l.curTokenLineNo,
		LinePos:        l.curTokenLinePos,
		LinePosBytes:   l.curTokenLinePosBytes,
		SourcePosBytes: l.curTokenSourcePosBytes}
	l.beginToken()
}

// beginToken begins the new token.
func (l *lexer) beginToken() {
	l.curTokenSourcePosBytes = l.sourcePosBytes
	l.curTokenSourcePos = l.sourcePos
	l.curTokenLineNo = l.lineNo
	l.curTokenLinePos = l.linePos
	l.curTokenLinePosBytes = l.linePosBytes
}

// Consume consumes the next rune in the source.
func (l *lexer) Consume() {
	if l.IsEos() {
		return
	}

	c, width := utf8.DecodeRuneInString(l.source[l.sourcePosBytes:])

	if c == '\n' {
		l.lineNo += 1
		l.linePos = 0
		l.linePosBytes = 0
	} else {
		l.linePos += 1
		l.linePosBytes += width
	}
	l.sourcePosBytes += width
	l.sourcePos += 1
}

// Peek returns the next rune in the source.
func (l *lexer) Peek() rune {
	// At the end of the string, there is no sane answer to Peek.
	if l.IsEos() {
		return utf8.RuneError
	}

	// If RuneError is returned, it will be handled as any other rune, likely
	// resulting in an ErrorIllegalChar token being emitted.
	char, _ := utf8.DecodeRuneInString(l.source[l.sourcePosBytes:])
	return char
}

// IsEos returns true if the whole source has been consumed false
// otherwise.
func (l *lexer) IsEos() bool {
	return l.sourcePosBytes >= len(l.source)
}

// run is the lexer's main loop.
func (l *lexer) run() {
	// We are implementing a state machine.
	// lexRoot is the beginning state.
	// nil is the end state.
	// States are functions which are called on the lexer. They return the
	// next state.
	for state := lexRoot; state != nil; {
		state = state(l)
	}
	close(l.tokens)
}

// A stateFn represents a state in the lexer state machine.
type stateFn func(*lexer) stateFn

// This is the beginning state and also the state which is returned to after
// most tokens are emitted.
func lexRoot(l *lexer) stateFn {
	if l.IsEos() {
		return nil
	}

	switch c := l.Peek(); {
	case isSingleCharTokens(c):
		return lexSingleCharTokens
	case isEqualsOrResponseStart(c):
		return lexEqualsOrResponse
	case isNameStart(c):
		return lexName
	case isOrdinalStart(c):
		return lexOrdinal
	case isNumberStart(c):
		return lexNumber
	case isStringStart(c):
		return lexString
	case isSkippable(c):
		return lexSkip
	case isMaybeComment(c):
		return lexComment
	}

	l.Consume()
	l.emitToken(ErrorIllegalChar)
	return nil
}

// isSkippable determines if a rune is skippable.
func isSkippable(c rune) bool {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n'
}

// lexSkip consumes skippable runes.
func lexSkip(l *lexer) stateFn {
	for isSkippable(l.Peek()) {
		l.beginToken()
		if l.Peek() == '\n' && l.linePos == 0 {
			l.emitToken(EmptyLine)
		}
		l.Consume()
	}
	l.beginToken()
	return lexRoot
}

// singleCharTokens is a map of single-rune tokens.
var singleCharTokens = map[rune]TokenKind{
	'(': LParen,
	')': RParen,
	'[': LBracket,
	']': RBracket,
	'{': LBrace,
	'}': RBrace,
	'<': LAngle,
	'>': RAngle,
	';': Semi,
	',': Comma,
	'.': Dot,
	'-': Minus,
	'+': Plus,
	'&': Amp,
	'?': Qstn,
}

// isSingleCharTokens returns true if the rune is a single character token.
func isSingleCharTokens(c rune) bool {
	_, ok := singleCharTokens[c]
	return ok
}

// lexSingleCharTokens lexes single character tokens.
func lexSingleCharTokens(l *lexer) stateFn {
	c := l.Peek()
	l.Consume()
	t, _ := singleCharTokens[c]
	l.emitToken(t)

	return lexRoot
}

// isEqualsOrResponseStart returns true if the rune corresponds to be
// beginning of either the '=' or '=>' tokens.
func isEqualsOrResponseStart(c rune) bool {
	return c == '='
}

// lexEqualsOrResponse lexes the '=' or the '=>' token.
func lexEqualsOrResponse(l *lexer) stateFn {
	l.Consume()

	if l.Peek() == '>' {
		l.Consume()
		l.emitToken(Response)
	} else {
		l.emitToken(Equals)
	}

	return lexRoot
}

// isAlpha returns true if the rune is a letter of the alphabet.
func isAlpha(c rune) bool {
	return (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z'))
}

// isDigit returns true if the rune is a digit.
func isDigit(c rune) bool {
	return ('0' <= c && c <= '9')
}

// isHexDigit returns true if the rune is a hexadecimal digit.
func isHexDigit(c rune) bool {
	return isDigit(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F')
}

// isNameStart returns true if the rune is the beginning of a Name token.
func isNameStart(c rune) bool {
	return isAlpha(c) || c == '_'
}

// keywordTokens maps keywords to their associate tokens.
var keywordTokens = map[string]TokenKind{
	"import":    Import,
	"module":    Module,
	"struct":    Struct,
	"union":     Union,
	"interface": Interface,
	"enum":      Enum,
	"const":     Const,
	"true":      True,
	"false":     False,
	"default":   Default,
}

// lexName lexes valid C identifiers. (K&R2: A.2.3)
func lexName(l *lexer) stateFn {
	l.Consume()

	// isNameRune returns true if the rune is valid in a Name token.
	isNameRune := func(c rune) bool {
		return isAlpha(c) || isDigit(c) || c == '_'
	}

	for isNameRune(l.Peek()) {
		l.Consume()
	}

	// Emit the appropriate keyword token if the current name is a
	// keyword or a Name token otherwise.
	if token, found := keywordTokens[l.CurText()]; found {
		l.emitToken(token)
	} else {
		l.emitToken(Name)
	}

	return lexRoot
}

// isOrdinalStart returns true if the rune is the beginning of an Ordinal
// token.
func isOrdinalStart(c rune) bool {
	return '@' == c
}

// lexOrdinal lexes an Ordinal token. Ordinals are a '@' followed by one
// or more digits.
func lexOrdinal(l *lexer) stateFn {
	// Consume the '@'.
	l.Consume()

	for isDigit(l.Peek()) {
		l.Consume()
	}

	l.emitToken(Ordinal)

	return lexRoot
}

// isNumberStart returns true if the rune is the beginning of a number.
func isNumberStart(c rune) bool {
	// Even hexadecimals must begin with a digit (namely 0).
	return isDigit(c)
}

// lexNumber lexes a number token.
func lexNumber(l *lexer) stateFn {
	// If the number begins with 0 it cannot be a decimal integer.
	if l.Peek() == '0' {
		return lexNumberStartWithZero
	}
	return lexDec
}

// lexDec lexes a base-10 number.
func lexDec(l *lexer) stateFn {
	for isDigit(l.Peek()) {
		l.Consume()
	}

	// Handle numbers with decimal points and numbers in scientific notation.
	switch c := l.Peek(); {
	case c == 'e' || c == 'E':
		return lexExponentPart
	case c == '.':
		return lexFractionPart
	}

	l.emitToken(IntConstDec)

	return lexRoot
}

// lexNumberStartWithZero lexes hexadecimals, some floats or 0.
func lexNumberStartWithZero(l *lexer) stateFn {
	// Consume the leading 0
	l.Consume()

	// Here we check to see whether we are in the hexadecimal or floating
	// point case.
	switch c := l.Peek(); {
	case c == 'x' || c == 'X':
		return lexHexNumber
	case c == '.':
		return lexFractionPart
	case c == 'e' || c == 'E':
		return lexExponentPart
	}

	// Found a naked 0.
	l.emitToken(IntConstDec)

	return lexRoot
}

// lexHexNumber lexes hexadecimal integers.
func lexHexNumber(l *lexer) stateFn {
	// Consume the x or X
	l.Consume()

	for isHexDigit(l.Peek()) {
		l.Consume()
	}

	l.emitToken(IntConstHex)

	return lexRoot
}

// lexFractionPart lexes the part of a floating point number that comes
// after the decimal point.
func lexFractionPart(l *lexer) stateFn {
	// Consume '.'
	l.Consume()

	for isDigit(l.Peek()) {
		l.Consume()
	}

	if c := l.Peek(); c == 'e' || c == 'E' {
		return lexExponentPart
	}

	l.emitToken(FloatConst)

	return lexRoot
}

// lexExponentPart lexes the part of a floating point number after E or e.
func lexExponentPart(l *lexer) stateFn {
	// Consume 'e' or 'E'
	l.Consume()

	if c := l.Peek(); c == '+' || c == '-' {
		l.Consume()
	}

	for isDigit(l.Peek()) {
		l.Consume()
	}

	l.emitToken(FloatConst)

	return lexRoot
}

// isStringStart returns true if the rune represents the beginning of a string.
func isStringStart(c rune) bool {
	return '"' == c
}

// lexString lexes a quoted string.
func lexString(l *lexer) stateFn {
	// Consume opening quotes.
	l.Consume()

	for !l.IsEos() && l.Peek() != '"' && l.Peek() != '\n' {
		if l.Peek() == '\\' {
			// If we see an escape character consume whatever follows blindly.
			// TODO(azani): Consider parsing escape sequences.
			l.Consume()
		}
		l.Consume()
	}

	if l.IsEos() || l.Peek() == '\n' {
		l.emitToken(ErrorUnterminatedStringLiteral)
		return nil
	}

	// Consume the closing quotes
	l.Consume()

	l.emitToken(StringLiteral)

	return lexRoot
}

// isMaybeComment returns true if the rune may be the beginning of a
// comment.
func isMaybeComment(c rune) bool {
	return c == '/'
}

// lexComment consumes a single-line or multi-line comment.
func lexComment(l *lexer) stateFn {
	// Consume the '/'.
	l.Consume()

	switch l.Peek() {
	case '/':
		return lexSingleLineComment
	case '*':
		return lexMultiLineComment
	}

	l.emitToken(ErrorIllegalChar)
	return nil
}

// lexSingleLineComment consumes a single line comment.
func lexSingleLineComment(l *lexer) stateFn {
	// Consume the '/'
	l.Consume()

	for !l.IsEos() && l.Peek() != '\n' {
		l.Consume()
	}

	l.emitToken(SingleLineComment)
	return lexRoot
}

// lexMultiLineComment consumes a multi-line comment.
func lexMultiLineComment(l *lexer) stateFn {
	// Consume the '*'.
	l.Consume()

	for !l.IsEos() {
		if l.Peek() == '*' {
			return lexPossibleEndOfComment
		}
		l.Consume()
	}

	l.emitToken(ErrorUnterminatedComment)
	return nil
}

// lexPossibleEndOfComment consumes the possible end of a multiline
// comment and determines whether the comment in fact ended or not.
func lexPossibleEndOfComment(l *lexer) stateFn {
	// Consume the '*'
	l.Consume()

	if l.IsEos() {
		l.emitToken(ErrorUnterminatedComment)
		return nil
	}

	if l.Peek() == '/' {
		l.Consume()
		l.emitToken(MultiLineComment)
		return lexRoot
	}

	return lexMultiLineComment
}
