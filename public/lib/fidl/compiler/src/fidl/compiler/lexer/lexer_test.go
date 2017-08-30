// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lexer

import (
	"strings"
	"testing"
)

func checkEq(t *testing.T, expected, actual interface{}) {
	if expected != actual {
		t.Fatalf("Failed check: Expected (%v), Actual (%v)", expected, actual)
	}
}

// pumpTokens pumps all the tokens from a channel into a slice.
func pumpTokens(tokensChan chan Token) []Token {
	tokens := []Token{}
	for token := range tokensChan {
		tokens = append(tokens, token)
	}
	return tokens
}

// TestAllSingleTokens tests for each token that a valid string is accepted as
// the correct token.
func TestAllSingleTokens(t *testing.T) {
	testData := []struct {
		source string
		token  TokenKind
	}{
		{"(", LParen},
		{")", RParen},
		{"[", LBracket},
		{"]", RBracket},
		{"{", LBrace},
		{"}", RBrace},
		{"<", LAngle},
		{">", RAngle},
		{";", Semi},
		{",", Comma},
		{".", Dot},
		{"-", Minus},
		{"+", Plus},
		{"&", Amp},
		{"?", Qstn},
		{"=", Equals},
		{"=>", Response},
		{"somet_hi3ng", Name},
		{"import", Import},
		{"module", Module},
		{"struct", Struct},
		{"union", Union},
		{"interface", Interface},
		{"enum", Enum},
		{"const", Const},
		{"true", True},
		{"false", False},
		{"default", Default},
		{"@10", Ordinal},
		{"10", IntConstDec},
		{"0", IntConstDec},
		{"0xA10", IntConstHex},
		{"0xa10", IntConstHex},
		{"0XA10", IntConstHex},
		{"0Xa10", IntConstHex},
		{"10.5", FloatConst},
		{"10e5", FloatConst},
		{"0.5", FloatConst},
		{"0e5", FloatConst},
		{"10e+5", FloatConst},
		{"10e-5", FloatConst},
		{"3.14E5", FloatConst},
		{"3.14e5", FloatConst},
		{"3.14E+55", FloatConst},
		{"3.14e+55", FloatConst},
		{"3.14E-55", FloatConst},
		{"3.14e-55", FloatConst},
		{"\"hello world\"", StringLiteral},
		{"\"hello \\\"real\\\" world\"", StringLiteral},
		{"// hello comment world", SingleLineComment},
		{"/* hello \n comment */", MultiLineComment},
	}

	for i := range testData {
		l := lexer{source: testData[i].source, tokens: make(chan Token)}
		go l.run()
		tokens := pumpTokens(l.tokens)

		if len(tokens) != 1 {
			t.Fatalf("Source('%v'): Expected 1 token but got %v instead: %v",
				testData[i].source, len(tokens), tokens)
		}

		checkEq(t, testData[i].source, tokens[0].Text)
		checkEq(t, testData[i].token, tokens[0].Kind)
	}
}

// TestTokenPosition tests that the position in the source string, the line
// number and the position in the line of the lexed token are correctly found.
func TestTokenPosition(t *testing.T) {
	source := "  \n  ."
	l := lexer{source: source, tokens: make(chan Token)}
	go l.run()
	tokens := pumpTokens(l.tokens)
	token := tokens[0]

	checkEq(t, 5, token.SourcePos)
	checkEq(t, 1, token.LineNo)
	checkEq(t, 2, token.LinePos)
}

// TestTokenPositionChineseString tests that SourcePos is expressed as a number
// of runes and that SourcePosBytes is expressed as a number of bytes.
func TestTokenPositionChineseString(t *testing.T) {
	source := "\"您好\" is"
	ts := Tokenize(source)
	checkEq(t, StringLiteral, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, 5, ts.PeekNext().SourcePos)
	checkEq(t, 9, ts.PeekNext().SourcePosBytes)
}

// TestSkipSkippable tests that all skippable characters are skipped.
func TestSkipSkippable(t *testing.T) {
	source := "  \t  \r \n  ."
	l := lexer{source: source, tokens: make(chan Token)}
	go l.run()
	tokens := pumpTokens(l.tokens)

	checkEq(t, Dot, tokens[0].Kind)
}

// TestTokenize tests that a single token embedded in a larger string is
// correctly lexed.
func TestTokenize(t *testing.T) {
	ts := Tokenize("   \t .   ")
	token := ts.PeekNext()
	checkEq(t, Dot, token.Kind)

	ts.ConsumeNext()
	token = ts.PeekNext()
	checkEq(t, EOF, token.Kind)
}

// TestTokenizeBadUTF8String tests that an invalid UTF8 string is handled.
func TestTokenizeBadUTF8String(t *testing.T) {
	ts := Tokenize("\xF0")
	checkEq(t, ErrorIllegalChar, ts.PeekNext().Kind)
}

// TestTokenizeEmptyString tests that empty strings are handled correctly.
func TestTokenizeEmptyString(t *testing.T) {
	ts := Tokenize("")
	checkEq(t, EOF, ts.PeekNext().Kind)
}

// TestTokenizeMoreThanOne tests that more than one token is correctly lexed.
func TestTokenizeMoreThanOne(t *testing.T) {
	ts := Tokenize("()")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, RParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, EOF, ts.PeekNext().Kind)
}

// TestIllegalChar tests that an illegal character is correctly spotted.
func TestIllegalChar(t *testing.T) {
	ts := Tokenize("   \t $   ")
	checkEq(t, ErrorIllegalChar, ts.PeekNext().Kind)
}

// TestUnterminatedStringLiteralEos tests that the correct error is emitted if
// a quoted string is never closed.
func TestUnterminatedStringLiteralEos(t *testing.T) {
	ts := Tokenize("\"hello world")
	checkEq(t, ErrorUnterminatedStringLiteral, ts.PeekNext().Kind)
}

// TestUnterminatedStringLiteralEol tests that the correct error is emitted if
// a quoted string is closed on a subsequent line.
func TestUnterminatedStringLiteralEol(t *testing.T) {
	ts := Tokenize("\"hello\n world\"")
	checkEq(t, ErrorUnterminatedStringLiteral, ts.PeekNext().Kind)
}

// TestSingleLineCommentNoSkip tests that single line comments are correctly lexed.
func TestSingleLineCommentNoSkip(t *testing.T) {
	ts := tokenizeUnfiltered("( // some stuff\n)")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, SingleLineComment, ts.PeekNext().Kind)
	checkEq(t, "// some stuff", ts.PeekNext().Text)
	ts.ConsumeNext()
	checkEq(t, RParen, ts.PeekNext().Kind)
}

// TestMultiLineCommentNoSkip tests that multi line comments are correctly lexed.
func TestMultiLineCommentNoSkip(t *testing.T) {
	ts := tokenizeUnfiltered("( /* hello world/  * *\n */)")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, MultiLineComment, ts.PeekNext().Kind)
	checkEq(t, "/* hello world/  * *\n */", ts.PeekNext().Text)
	ts.ConsumeNext()
	checkEq(t, RParen, ts.PeekNext().Kind)
}

// TestSingleLineComment tests that single line comments are correctly skipped.
func TestSingleLineComment(t *testing.T) {
	ts := Tokenize("( // some stuff\n)")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, RParen, ts.PeekNext().Kind)
}

// TestMultiLineComment tests that multi line comments are correctly skipped.
func TestMultiLineComment(t *testing.T) {
	ts := Tokenize("( /* hello world/  * *\n */)")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, RParen, ts.PeekNext().Kind)
}

// TestConsumeUnfilteredOnly tests that ConsumeNext consumes only unfiltered tokens.
func TestConsumeUnfilteredOnly(t *testing.T) {
	ts := Tokenize(" /* hello world*/  ( )")
	// ConsumeNext should consume the left parenthesis, not the filtered comment.
	ts.ConsumeNext()
	checkEq(t, RParen, ts.PeekNext().Kind)
}

// TestCommentsOnly tests that source made only of comments is correctly processed.
func TestCommentsOnly(t *testing.T) {
	ts := Tokenize("/* hello world */\n  // hello world")
	checkEq(t, EOF, ts.PeekNext().Kind)
}

// TestUnterminatedMultiLineComment tests that unterminated multiline comments
// emit the correct error.
func TestUnterminatedMultiLineComment(t *testing.T) {
	ts := Tokenize("( /* hello world/  * *\n )")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, ErrorUnterminatedComment, ts.PeekNext().Kind)
}

// TestUnterminatedMultiLineCommentAtStar tests that if the string ends at a *
// (which could be the beginning of the close of a multiline comment) the right
// error is emitted.
func TestUnterminatedMultiLineCommentAtStar(t *testing.T) {
	ts := Tokenize("( /* hello world/  *")
	checkEq(t, LParen, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, ErrorUnterminatedComment, ts.PeekNext().Kind)
}

// TestTokenSnippet tests snippet generation based on tokens.
func TestTokenSnippet(t *testing.T) {
	source := "\n hello world \n"
	ts := Tokenize(source)
	expected := " hello world \n"
	expected += " ^^^^^"
	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

// TestTokenSnippetNoNewLines tests that the correct snippet is generated when
// the source does not have new lines.
func TestTokenSnippetNoNewLines(t *testing.T) {
	source := " hello world "
	ts := Tokenize(source)
	expected := " hello world \n"
	expected += " ^^^^^"
	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

// TestTokenSnippetNotFirst tests snippet generation for a token that is not
// first on the line.
func TestTokenSnippetNotFirst(t *testing.T) {
	source := "hello world"
	ts := Tokenize(source)
	expected := "hello world\n"
	expected += "      ^^^^^"
	ts.ConsumeNext()

	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

// TestTokenSnippetNotASCII tests snippet generation in the presence of
// non-ASCII characters.
func TestTokenSnippetNotASCII(t *testing.T) {
	source := "\"你好\" \"世界\""
	ts := Tokenize(source)
	expected := "\"你好\" \"世界\"\n"
	expected += "     ^^^^"
	ts.ConsumeNext()

	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

// TestTokenSnippetLongPrelude tests snippet generation in the presence of a
// long line with many characters preceeding the token.
func TestTokenSnippetLongPrelude(t *testing.T) {
	source := strings.Repeat(" ", 70) + "hello world"
	ts := Tokenize(source)
	expected := strings.Repeat(" ", 58) + "hello world\n"
	expected += strings.Repeat(" ", 58) + "^^^^^"
	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

// TestTokenSnippetLongPreludeWithNewLine tests snippet generation in the presence of a
// long line with many characters preceeding the token and then a newline after the token.
func TestTokenSnippetLongPreludeWithNewLine(t *testing.T) {
	source := strings.Repeat(" ", 70) + "hello world\n"
	ts := Tokenize(source)
	expected := strings.Repeat(" ", 58) + "hello world\n"
	expected += strings.Repeat(" ", 58) + "^^^^^"
	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

// TestTokenSnippetLongPreludeWithSuffix tests snippet generation in the presence of a
// long line with many characters preceeding the token and then some characters following on the same line.
func TestTokenSnippetLongPreludeWithLongSuffix(t *testing.T) {
	source := strings.Repeat(" ", 70) + "hello world " + strings.Repeat("x", 10)
	ts := Tokenize(source)
	expected := strings.Repeat(" ", 58) + "hello world xxxxxxxx\n"
	expected += strings.Repeat(" ", 58) + "^^^^^"
	checkEq(t, expected, ts.PeekNext().Snippet(source, false))
}

func TestFilteredTokens(t *testing.T) {
	source := "/* filtered1 */ hello world // filtered2"
	ts := Tokenize(source)
	ts.ConsumeNext()
	ts.ConsumeNext()
	filtered := ts.(*FilteredTokenStream).FilteredTokens()
	checkEq(t, MultiLineComment, filtered[0].Kind)
	checkEq(t, SingleLineComment, filtered[1].Kind)
}

func TestEmptyLine(t *testing.T) {
	ts := tokenizeUnfiltered("\n")
	checkEq(t, EmptyLine, ts.PeekNext().Kind)
	ts.ConsumeNext()
	checkEq(t, EOF, ts.PeekNext().Kind)
}
