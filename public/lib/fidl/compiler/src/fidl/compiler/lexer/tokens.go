// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TokenKinds is a type which describes the kinds of tokens which can be
// encountered in a mojom file.

package lexer

import (
	"bytes"
	"fmt"
	"strings"
	"unicode/utf8"
)

type TokenKind int

// TokenKinds
const (
	// A character was found which is not part of a valid token.
	ErrorIllegalChar TokenKind = iota
	// A quoted string was opened but not closed.
	ErrorUnterminatedStringLiteral
	// A multiline comment was opened but not closed.
	ErrorUnterminatedComment
	// Indicates the end of a stream of tokens.
	EOF

	// Punctuators and Separators
	LParen
	RParen
	LBracket
	RBracket
	LBrace
	RBrace
	LAngle
	RAngle
	Semi
	Comma
	Dot
	Minus
	Plus
	Amp
	Qstn
	Equals
	Response

	// Names
	Name

	// Keywords
	Import
	Module
	Struct
	Union
	Interface
	Enum
	Const
	True
	False
	Default

	// Constants
	IntConstDec
	IntConstHex
	FloatConst
	Ordinal
	StringLiteral

	// Comments
	SingleLineComment
	MultiLineComment

	// Empty Line
	EmptyLine
)

// This method is used to generate user-facing strings in compilation error
// messages. For example for LBrace we produce the string "'{'". Notice the
// single-quotes. This will be used for example in an error message that looks
// like the following:
// Unexpected token at line 5, column 6: '###'. Expecting '{'.
func (tokenKind TokenKind) String() string {
	switch tokenKind {
	// Errors
	case ErrorIllegalChar:
		return "illegal token"
	case ErrorUnterminatedStringLiteral:
		return "unterminated string literal"
	case ErrorUnterminatedComment:
		return "unterminated comment"

	// End of file
	case EOF:
		return "eof"

	// Punctuators and Separators
	case LParen:
		return "'('"
	case RParen:
		return "')'"
	case LBracket:
		return "'['"
	case RBracket:
		return "']'"
	case LBrace:
		return "'{'"
	case RBrace:
		return "'}'"
	case LAngle:
		return "'<'"
	case RAngle:
		return "'>'"
	case Semi:
		return "';'"
	case Comma:
		return "','"
	case Dot:
		return "'.'"
	case Minus:
		return "'-'"
	case Plus:
		return "'+'"
	case Amp:
		return "'&'"
	case Qstn:
		return "'?'"
	case Equals:
		return "'='"
	case Response:
		return "'=>'"

	// Names
	case Name:
		return "a name"

	// Keywords
	case Import:
		return "'import'"
	case Module:
		return "'module'"
	case Struct:
		return "'struct'"
	case Union:
		return "'union'"
	case Interface:
		return "'interface'"
	case Enum:
		return "'enum'"
	case Const:
		return "'const'"
	case True:
		return "'true'"
	case False:
		return "'false'"
	case Default:
		return "'default'"

	// Constants
	case IntConstDec:
		return "decimal integer literal"
	case IntConstHex:
		return "hex integer literal"
	case FloatConst:
		return "float literal"
	case Ordinal:
		return "an ordinal"
	case StringLiteral:
		return "a string literal"

	case SingleLineComment:
		return "single line comment"
	case MultiLineComment:
		return "multi line comment"

	case EmptyLine:
		return "empty line"

	default:
		// Note(rudominer) It is important to use %d below so as to avoid
		// re-invoking this method and causing an infinite recursion.
		return fmt.Sprintf("%d", tokenKind)
	}
}

type Token struct {
	Kind TokenKind
	Text string
	// SourcePos is the number of runes preceeding the token.
	SourcePos int
	// LinePos is the number of runes preceeding the token on its line.
	LinePos int
	// SourcePosBytes is the number of bytes preceeding the token.
	SourcePosBytes int
	// LinePosBytes is the number of bytes preceeding the token on its line.
	LinePosBytes int
	// LineNo is the line on which the token is found. (First line is 0.)
	LineNo int
}

// ShortLocationString is used to generate user-facing strings in compilation
// error messages. This will be used for example in an error message that looks
// like the following:
// Unexpected token at line 5, column 6: '###'. Expecting '{'.
func (t Token) ShortLocationString() string {
	return fmt.Sprintf("%d:%d", t.LineNo+1, t.LinePos+1)
}

func (t Token) LongLocationString() string {
	return fmt.Sprintf("line %d, column %d", t.LineNo+1, t.LinePos+1)
}

// EOF returns true if the token on which it is called represents the end of the
// token string.
func (t Token) EOF() bool {
	return t.Kind == EOF
}

// String is used to generate user-facing strings in compilation error
// messages. For many token kinds the TokenKind.String() method will produce
// good results for representing the token. But for other TokenKinds we will
// want to include some information besides a representation of the kind.
// For example for an ErrorIllegalChar kind we wnat to show the text.
// This will be used for example in an error message that looks
// like the following:
// Unexpected token at line 5, column 6: '###'. Expecting '{'.
func (token Token) String() string {
	switch token.Kind {
	case StringLiteral:
		return token.Text
	case Name, IntConstDec, IntConstHex, FloatConst, Ordinal, ErrorIllegalChar:
		return fmt.Sprintf("%q", token.Text)

	default:
		return token.Kind.String()
	}
}

// Snippet is used to generate a user-facing string in compilation error
// messages. It displays the token's text as well as the surrounding line for
// context. It also includes a line with some carets to highlight the token.
// source is the source code where token was found.
// If color is true, the carets on the second line will be colored.
func (token Token) Snippet(source string, color bool) (snippet string) {
	begin := token.SourcePosBytes - token.LinePosBytes

	// First, we make sure the prelude to the token is not too long. Since we
	// limit the width of the snippet to 79 runes, we first make sure the prelude
	// takes up no more than 58 of those runes.
	runeCount := token.LinePos
	if runeCount > 58 {
		runeCount = 58
	}
	skipRunes := token.LinePos - runeCount
	for index, _ := range source[begin:] {
		skipRunes--
		if skipRunes == 0 {
			begin += (index + 1)
			break
		}
	}

	// Now we calculate the end of the snippet line. Either the first new line
	// rune or a total of no more than 79 characters.
	end := len(source)
	for index, rune := range source[token.SourcePosBytes:] {
		if rune == '\n' || runeCount >= 78 {
			end = index + token.SourcePosBytes
			break
		}
		runeCount++
	}

	snippetBuffer := bytes.NewBufferString(source[begin:end])
	snippetBuffer.WriteRune('\n')

	// We calculate how much whitespace to add before the caret marker for the
	// token. Please note that there is an assumption that all non-tab characters
	// are of the same width. This is not correct, but close-enough most of the
	// time.
	for _, rune := range source[begin:token.SourcePosBytes] {
		if rune == '\t' {
			snippetBuffer.WriteRune('\t')
		} else {
			snippetBuffer.WriteRune(' ')
		}
	}

	// We don't want too big of a caret line as that may be distracting. So we
	// limit it to 20 runes at most.
	tokenSize := utf8.RuneCountInString(token.Text)
	if token.Kind == ErrorUnterminatedComment {
		// In the case of an unterminated comment, the token text will contain
		// the entire rest of the source after the opening comment. We really
		// only want to highlight the "/*" characters marking the beginning of
		// the comment.
		tokenSize = 2
	} else if tokenSize > 20 {
		tokenSize = 20
	}
	if color {
		// Set the caret characters to green.
		snippetBuffer.WriteString("\x1b[32;1m")
	}
	snippetBuffer.WriteString(strings.Repeat("^", tokenSize))
	if color {
		// Reset all printing attributes.
		snippetBuffer.WriteString("\x1b[0m")
	}
	snippet = snippetBuffer.String()
	return
}

func StringLiteralTokenToText(token Token) string {
	if token.Kind != StringLiteral {
		panic("stringLiteralTokenToText can only accept StringLiteral tokens.")
	}

	text := token.Text
	length := len(text)
	if (length < 2) || (text[0] != '"') || (text[length-1] != '"') {
		panic(fmt.Sprintf("Lexer returned a string literal token whose "+
			"text was not delimited by quotation marks: '%s'.", text))
	}
	return text[1 : length-1]
}
