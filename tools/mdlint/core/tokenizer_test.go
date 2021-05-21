// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fmt"
	"io"
	"strconv"
	"strings"
	"testing"
)

func assertNoErr(t *testing.T, err error) {
	if err != nil {
		t.Errorf("no error expected, found: %s", err)
	}
}

type docUnderTest struct {
	t *testing.T
	*Doc
}

func (d *docUnderTest) checkRead(expected rune) {
	d.t.Helper()
	actual, err := d.Doc.readRune()
	if err != nil {
		d.t.Fatalf("encountered err=%v", err)
	}
	if expected != actual {
		d.t.Errorf("expected=%c, actual=%c", expected, actual)
	}
}

func (d *docUnderTest) checkReadLeadsToEOF() {
	d.t.Helper()
	_, err := d.Doc.readRune()
	if err != io.EOF {
		d.t.Fatalf("expected to reach EOF, instead go err=%v", err)
	}
}

func (d *docUnderTest) checkUnread() {
	d.t.Helper()
	if err := d.Doc.unreadRune(); err != nil {
		d.t.Fatalf("encountered err=%v", err)
	}
}

func TestDocReadAndUnread(t *testing.T) {
	doc := docUnderTest{t: t, Doc: newDoc("filename", strings.NewReader("01234567abcd"))}

	// Fill the ring buffer, read two more runes, then check unreads & reads.
	for i := 0; i < runesBufLen; i++ {
		doc.checkRead(rune(int('0') + i))
	}
	doc.checkRead('a')
	doc.checkRead('b')
	doc.checkUnread() // b
	doc.checkUnread() // a
	doc.checkUnread() // 7
	doc.checkRead('7')
	doc.checkRead('a')
	doc.checkUnread() // a
	doc.checkUnread() // 7
	doc.checkUnread() // 6
	doc.checkUnread() // 5
	doc.checkUnread() // 4
	doc.checkUnread() // 3

	// Check reads until EOF. In doing so, we verify that we properly switch
	// from reading from the ring buffer to reading from the stream. We then
	// unread, and read again to verify that the ring buffer has been properly
	// filled.
	for i := 3; i < runesBufLen; i++ {
		doc.checkRead(rune(int('0') + i))
	}
	doc.checkRead('a')
	doc.checkRead('b')
	doc.checkRead('c')
	doc.checkRead('d')
	doc.checkReadLeadsToEOF()
	doc.checkUnread() // d
	doc.checkUnread() // c
	doc.checkUnread() // b
	doc.checkUnread() // a
	doc.checkRead('a')
	doc.checkRead('b')
	doc.checkRead('c')
	doc.checkRead('d')

	// Now, we unread past the end and ensure that we properly fail.
	for i := 0; i < runesBufLen-1; i++ {
		doc.checkUnread()
	}
	err := doc.Doc.unreadRune()
	if err == nil {
		t.Fatalf("expected to fail on too many unread attempts")
	}

	// Last, we read forward again to EOF thus verifying the whole content of
	// the ring buffer.
	for i := 5; i < runesBufLen; i++ {
		doc.checkRead(rune(int('0') + i))
	}
	doc.checkRead('a')
	doc.checkRead('b')
	doc.checkRead('c')
	doc.checkRead('d')
	doc.checkReadLeadsToEOF()
	doc.checkReadLeadsToEOF()
}

func TestTokenizer(t *testing.T) {
	for inputNum, ex := range tokenizerTestCases {
		ex.runTestTokenization(t, inputNum)
	}
}

type expectedToken struct {
	Kind    TokenKind
	Content string
}

type tokenizerTestCase struct {
	input  string
	tokens []expectedToken
}

func (ex tokenizerTestCase) forEachToken(t *testing.T, callback func(tokenNum int, expected expectedToken, actual Token)) {
	var (
		tokenizer = newTokenizer(newDoc("filename", strings.NewReader(ex.input)))
		tokenNum  int
	)
	for {
		actual, err := tokenizer.next()
		if err != nil {
			t.Fatalf("unexpected tokenization error: %s", err)
		}
		if tokenNum == len(ex.tokens) {
			t.Errorf("%d: no additional tokens expected, but found %+v", tokenNum, actual)
			return
		}
		expected := ex.tokens[tokenNum]
		callback(tokenNum, expected, actual)
		if actual.Kind == EOF {
			return
		}
		tokenNum++
	}
}

func (ex tokenizerTestCase) runTestTokenization(t *testing.T, inputNum int) {
	t.Run(fmt.Sprintf("input #%d", inputNum), func(t *testing.T) {
		t.Log(strconv.Quote(ex.input))
		var ln, col int = 1, 1
		ex.forEachToken(t, func(tokenNum int, expected expectedToken, actual Token) {
			// Verify kind, then content.
			if expected.Kind != actual.Kind {
				t.Errorf("%d: expecting %+v, found %+v", tokenNum, expected.Kind, actual)
			} else if expected.Content != actual.Content {
				t.Errorf("%d: expecting '%+v', found '%+v'", tokenNum, expected.Content, actual)
			}

			// Verify position.
			if actual.Ln != ln || actual.Col != col {
				t.Errorf("%d: expecting position %d:%d, found position %d:%d", tokenNum, ln, col, actual.Ln, actual.Col)
			}
			if actual.Kind == Newline {
				ln, col = ln+1, 1
			} else {
				lines := strings.Split(actual.Content, "\n")
				if numNewLines := len(lines) - 1; numNewLines == 0 {
					col += len(actual.Content)
				} else {
					ln += numNewLines
					col = len(lines[numNewLines]) + 1
				}
			}
		})
	})
}

var tokenizerTestCases = []tokenizerTestCase{
	{
		input: "this is some#text #notaheader",
		tokens: []expectedToken{
			{Kind: Text, Content: "this"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "is"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "some#text"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "#notaheader"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "# Title\n##Second \t  title  \n\t",
		tokens: []expectedToken{
			{Kind: Header, Content: "#"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "Title"},
			{Kind: Newline, Content: "\n"},
			{Kind: Header, Content: "##"},
			{Kind: Text, Content: "Second"},
			{Kind: Space, Content: " \t  "},
			{Kind: Text, Content: "title"},
			{Kind: Space, Content: "  "},
			{Kind: Newline, Content: "\n"},
			{Kind: Space, Content: "\t"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "before\n### Header {#anchor}\nafter\t{#not-an-anchor}",
		tokens: []expectedToken{
			{Kind: Text, Content: "before"},
			{Kind: Newline, Content: "\n"},
			{Kind: Header, Content: "###"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "Header"},
			{Kind: Space, Content: " "},
			{Kind: Anchor, Content: "{#anchor}"},
			{Kind: Newline, Content: "\n"},
			{Kind: Text, Content: "after"},
			{Kind: Space, Content: "\t"},
			{Kind: Text, Content: "{#not-an-anchor}"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "here [a link][with ref] or [another](with url) or [link and ref] (but-this-is-url-too)",
		tokens: []expectedToken{
			{Kind: Text, Content: "here"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[a link]"},
			{Kind: Link, Content: "[with ref]"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "or"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[another]"},
			{Kind: URL, Content: "(with url)"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "or"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[link and ref]"},
			{Kind: Space, Content: " "},
			{Kind: URL, Content: "(but-this-is-url-too)"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "[link]\t(url) and   [link2]   \t\t (url2)",
		tokens: []expectedToken{
			{Kind: Link, Content: "[link]"},
			{Kind: Space, Content: "\t"},
			{Kind: URL, Content: "(url)"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "and"},
			{Kind: Space, Content: "   "},
			{Kind: Link, Content: "[link2]"},
			{Kind: Space, Content: "   \t\t "},
			{Kind: URL, Content: "(url2)"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "[[broken1] or [broken2]]",
		tokens: []expectedToken{
			{Kind: Link, Content: "[[broken1]"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "or"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[broken2]"},
			{Kind: Text, Content: "]"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "a-link [on\nmultiple\nlines] is-still-just-one-link",
		tokens: []expectedToken{
			{Kind: Text, Content: "a-link"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[on\nmultiple\nlines]"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "is-still-just-one-link"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "a-link (in [parenthesis]) is-still-just-one-link",
		tokens: []expectedToken{
			{Kind: Text, Content: "a-link"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "(in"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[parenthesis]"},
			{Kind: Text, Content: ")"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "is-still-just-one-link"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "only [`[a]`] link token",
		tokens: []expectedToken{
			{Kind: Text, Content: "only"},
			{Kind: Space, Content: " "},
			{Kind: Link, Content: "[`[a]`]"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "link"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "token"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "the code `matrix[i][j]` is not a link",
		tokens: []expectedToken{
			{Kind: Text, Content: "the"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "code"},
			{Kind: Space, Content: " "},
			{Kind: CodeBlock, Content: "`matrix[i][j]`"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "is"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "not"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "a"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "link"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "* list\n * list-too\nbut * not-a-list",
		tokens: []expectedToken{
			{Kind: List, Content: "*"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "list"},
			{Kind: Newline, Content: "\n"},
			{Kind: Space, Content: " "},
			{Kind: List, Content: "*"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "list-too"},
			{Kind: Newline, Content: "\n"},
			{Kind: Text, Content: "but"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "*"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "not-a-list"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "- list\n\t\t  - list-too\nbut\t\t- not-a-list",
		tokens: []expectedToken{
			{Kind: List, Content: "-"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "list"},
			{Kind: Newline, Content: "\n"},
			{Kind: Space, Content: "\t\t  "},
			{Kind: List, Content: "-"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "list-too"},
			{Kind: Newline, Content: "\n"},
			{Kind: Text, Content: "but"},
			{Kind: Space, Content: "\t\t"},
			{Kind: Text, Content: "-"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "not-a-list"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "1. Numbered-list\n1. Here-too\n345. So-long\nNot-this-one 7.",
		tokens: []expectedToken{
			{Kind: List, Content: "1."},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "Numbered-list"},
			{Kind: Newline, Content: "\n"},
			{Kind: List, Content: "1."},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "Here-too"},
			{Kind: Newline, Content: "\n"},
			{Kind: List, Content: "345."},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "So-long"},
			{Kind: Newline, Content: "\n"},
			{Kind: Text, Content: "Not-this-one"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "7."},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "1234. Yes\n123456789101112. YES!",
		tokens: []expectedToken{
			{Kind: List, Content: "1234."},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "Yes"},
			{Kind: Newline, Content: "\n"},
			{Kind: List, Content: "123456789101112."},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "YES!"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "before  ```this[not-a-link]```\tafter",
		tokens: []expectedToken{
			{Kind: Text, Content: "before"},
			{Kind: Space, Content: "  "},
			{Kind: FencedCodeBlock, Content: "```this[not-a-link]```"},
			{Kind: Space, Content: "\t"},
			{Kind: Text, Content: "after"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "before\n```\nthis\nhas\nmultiple\nlines\n```\nafter",
		tokens: []expectedToken{
			{Kind: Text, Content: "before"},
			{Kind: Newline, Content: "\n"},
			{Kind: FencedCodeBlock, Content: "```\nthis\nhas\nmultiple\nlines\n```"},
			{Kind: Newline, Content: "\n"},
			{Kind: Text, Content: "after"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "write `hello`, ``, and `world` nope",
		tokens: []expectedToken{
			{Kind: Text, Content: "write"},
			{Kind: Space, Content: " "},
			{Kind: CodeBlock, Content: "`hello`"},
			{Kind: Text, Content: ","},
			{Kind: Space, Content: " "},
			{Kind: CodeBlock, Content: "``"},
			{Kind: Text, Content: ","},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "and"},
			{Kind: Space, Content: " "},
			{Kind: CodeBlock, Content: "`world`"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "nope"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "a \"`Usage: string`\" should-be-code",
		tokens: []expectedToken{
			{Kind: Text, Content: "a"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: `"`},
			{Kind: CodeBlock, Content: "`Usage: string`"},
			{Kind: Text, Content: `"`},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "should-be-code"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "this **is-not-a-list**",
		tokens: []expectedToken{
			{Kind: Text, Content: "this"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "**is-not-a-list**"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "hello\n\n\nworld",
		tokens: []expectedToken{
			{Kind: Text, Content: "hello"},
			{Kind: Newline, Content: "\n"},
			{Kind: Newline, Content: "\n"},
			{Kind: Newline, Content: "\n"},
			{Kind: Text, Content: "world"},
			{Kind: EOF, Content: ""},
		},
	},
	{
		input: "some {{ var }} and {% code %} here {{ } }} then {##}",
		tokens: []expectedToken{
			{Kind: Text, Content: "some"},
			{Kind: Space, Content: " "},
			{Kind: JinjaExpression, Content: "{{ var }}"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "and"},
			{Kind: Space, Content: " "},
			{Kind: JinjaStatement, Content: "{% code %}"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "here"},
			{Kind: Space, Content: " "},
			{Kind: JinjaExpression, Content: "{{ } }}"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "then"},
			{Kind: Space, Content: " "},
			{Kind: JinjaComment, Content: "{##}"},
			{Kind: EOF, Content: ""},
		},
	},

	// codebase examples

	{
		input: "<!-- xref -->\n\n[`zx_vmo_create()`]: vmo_create.md\n",
		tokens: []expectedToken{
			// TODO(fxbug.dev/62964): need to recognize HTML elements, and in
			// particular comments
			{Kind: Text, Content: "<!--"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "xref"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "-->"},

			{Kind: Newline, Content: "\n"},
			{Kind: Newline, Content: "\n"},
			{Kind: Link, Content: "[`zx_vmo_create()`]"},
			{Kind: Text, Content: ":"},
			{Kind: Space, Content: " "},
			{Kind: Text, Content: "vmo_create.md"},
			{Kind: Newline, Content: "\n"},
			{Kind: EOF, Content: ""},
		},
	},
}
