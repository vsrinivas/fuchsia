// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

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
	*doc
}

func (d *docUnderTest) checkRead(expected rune) {
	d.t.Helper()
	actual, err := d.doc.readRune()
	if err != nil {
		d.t.Fatalf("encountered err=%v", err)
	}
	if expected != actual {
		d.t.Errorf("expected=%c, actual=%c", expected, actual)
	}
}

func (d *docUnderTest) checkReadLeadsToEOF() {
	d.t.Helper()
	_, err := d.doc.readRune()
	if err != io.EOF {
		d.t.Fatalf("expected to reach EOF, instead go err=%v", err)
	}
}

func (d *docUnderTest) checkUnread() {
	d.t.Helper()
	if err := d.doc.unreadRune(); err != nil {
		d.t.Fatalf("encountered err=%v", err)
	}
}

func TestDocReadAndUnread(t *testing.T) {
	doc := docUnderTest{t: t, doc: newDoc("filename", strings.NewReader("01234567abcd"))}

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
	err := doc.doc.unreadRune()
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
	kind    tokenKind
	content string
}

type tokenizerTestCase struct {
	input  string
	tokens []expectedToken
}

func (ex tokenizerTestCase) forEachToken(t *testing.T, callback func(tokenNum int, expected expectedToken, actual token)) {
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
		if actual.kind == tEOF {
			return
		}
		tokenNum++
	}
}

func (ex tokenizerTestCase) runTestTokenization(t *testing.T, inputNum int) {
	t.Run(fmt.Sprintf("input #%d", inputNum), func(t *testing.T) {
		t.Log(strconv.Quote(ex.input))
		var ln, col int = 1, 1
		ex.forEachToken(t, func(tokenNum int, expected expectedToken, actual token) {
			// Verify kind, then content.
			if expected.kind != actual.kind {
				t.Errorf("%d: expecting %+v, found %+v", tokenNum, expected.kind, actual)
			} else if expected.content != actual.content {
				t.Errorf("%d: expecting '%+v', found '%+v'", tokenNum, expected.content, actual)
			}

			// Verify position.
			if actual.ln != ln || actual.col != col {
				t.Errorf("%d: expecting position %d:%d, found position %d:%d", tokenNum, ln, col, actual.ln, actual.col)
			}
			if actual.kind == tNewline {
				ln, col = ln+1, 1
			} else {
				lines := strings.Split(actual.content, "\n")
				if numNewLines := len(lines) - 1; numNewLines == 0 {
					col += len(actual.content)
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
			{kind: tText, content: "this"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "is"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "some#text"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "#notaheader"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "# Title\n##Second \t  title  \n\t",
		tokens: []expectedToken{
			{kind: tHeader, content: "#"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "Title"},
			{kind: tNewline, content: "\n"},
			{kind: tHeader, content: "##"},
			{kind: tText, content: "Second"},
			{kind: tSpace, content: " \t  "},
			{kind: tText, content: "title"},
			{kind: tSpace, content: "  "},
			{kind: tNewline, content: "\n"},
			{kind: tSpace, content: "\t"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "before\n### Header {#anchor}\nafter\t{#not-an-anchor}",
		tokens: []expectedToken{
			{kind: tText, content: "before"},
			{kind: tNewline, content: "\n"},
			{kind: tHeader, content: "###"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "Header"},
			{kind: tSpace, content: " "},
			{kind: tAnchor, content: "{#anchor}"},
			{kind: tNewline, content: "\n"},
			{kind: tText, content: "after"},
			{kind: tSpace, content: "\t"},
			{kind: tText, content: "{#not-an-anchor}"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "here [a link][with ref] or [another](with url) or [link and ref] (but-this-is-url-too)",
		tokens: []expectedToken{
			{kind: tText, content: "here"},
			{kind: tSpace, content: " "},
			{kind: tLink, content: "[a link]"},
			{kind: tLink, content: "[with ref]"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "or"},
			{kind: tSpace, content: " "},
			{kind: tLink, content: "[another]"},
			{kind: tURL, content: "(with url)"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "or"},
			{kind: tSpace, content: " "},
			{kind: tLink, content: "[link and ref]"},
			{kind: tSpace, content: " "},
			{kind: tURL, content: "(but-this-is-url-too)"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "[link]\t(url) and   [link2]   \t\t (url2)",
		tokens: []expectedToken{
			{kind: tLink, content: "[link]"},
			{kind: tSpace, content: "\t"},
			{kind: tURL, content: "(url)"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "and"},
			{kind: tSpace, content: "   "},
			{kind: tLink, content: "[link2]"},
			{kind: tSpace, content: "   \t\t "},
			{kind: tURL, content: "(url2)"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "[[broken1] or [broken2]]",
		tokens: []expectedToken{
			{kind: tLink, content: "[[broken1]"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "or"},
			{kind: tSpace, content: " "},
			{kind: tLink, content: "[broken2]"},
			{kind: tText, content: "]"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "a-link [on\nmultiple\nlines] is-still-just-one-link",
		tokens: []expectedToken{
			{kind: tText, content: "a-link"},
			{kind: tSpace, content: " "},
			{kind: tLink, content: "[on\nmultiple\nlines]"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "is-still-just-one-link"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "* list\n * list-too\nbut * not-a-list",
		tokens: []expectedToken{
			{kind: tList, content: "*"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "list"},
			{kind: tNewline, content: "\n"},
			{kind: tSpace, content: " "},
			{kind: tList, content: "*"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "list-too"},
			{kind: tNewline, content: "\n"},
			{kind: tText, content: "but"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "*"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "not-a-list"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "- list\n\t\t  - list-too\nbut\t\t- not-a-list",
		tokens: []expectedToken{
			{kind: tList, content: "-"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "list"},
			{kind: tNewline, content: "\n"},
			{kind: tSpace, content: "\t\t  "},
			{kind: tList, content: "-"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "list-too"},
			{kind: tNewline, content: "\n"},
			{kind: tText, content: "but"},
			{kind: tSpace, content: "\t\t"},
			{kind: tText, content: "-"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "not-a-list"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "before  ```this[not-a-link]```\tafter",
		tokens: []expectedToken{
			{kind: tText, content: "before"},
			{kind: tSpace, content: "  "},
			{kind: tCode, content: "```this[not-a-link]```"},
			{kind: tSpace, content: "\t"},
			{kind: tText, content: "after"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "before\n```\nthis\nhas\nmultiple\nlines\n```\nafter",
		tokens: []expectedToken{
			{kind: tText, content: "before"},
			{kind: tNewline, content: "\n"},
			{kind: tCode, content: "```\nthis\nhas\nmultiple\nlines\n```"},
			{kind: tNewline, content: "\n"},
			{kind: tText, content: "after"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "write `hello`, ``, and `world` nope",
		tokens: []expectedToken{
			{kind: tText, content: "write"},
			{kind: tSpace, content: " "},
			{kind: tCode, content: "`hello`"},
			{kind: tText, content: ","},
			{kind: tSpace, content: " "},
			{kind: tCode, content: "``"},
			{kind: tText, content: ","},
			{kind: tSpace, content: " "},
			{kind: tText, content: "and"},
			{kind: tSpace, content: " "},
			{kind: tCode, content: "`world`"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "nope"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "a \"`Usage: string`\" should-be-code",
		tokens: []expectedToken{
			{kind: tText, content: "a"},
			{kind: tSpace, content: " "},
			{kind: tText, content: `"`},
			{kind: tCode, content: "`Usage: string`"},
			{kind: tText, content: `"`},
			{kind: tSpace, content: " "},
			{kind: tText, content: "should-be-code"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "this **is-not-a-list**",
		tokens: []expectedToken{
			{kind: tText, content: "this"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "**is-not-a-list**"},
			{kind: tEOF, content: ""},
		},
	},
	{
		input: "hello\n\n\nworld",
		tokens: []expectedToken{
			{kind: tText, content: "hello"},
			{kind: tNewline, content: "\n"},
			{kind: tNewline, content: "\n"},
			{kind: tNewline, content: "\n"},
			{kind: tText, content: "world"},
			{kind: tEOF, content: ""},
		},
	},

	// codebase examples

	{
		input: "<!-- xref -->\n\n[`zx_vmo_create()`]: vmo_create.md\n",
		tokens: []expectedToken{
			// TODO(fxbug.dev/62964): need to recognize HTML elements, and in
			// particular comments
			{kind: tText, content: "<!--"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "xref"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "-->"},

			{kind: tNewline, content: "\n"},
			{kind: tNewline, content: "\n"},
			{kind: tLink, content: "[`zx_vmo_create()`]"},
			{kind: tText, content: ":"},
			{kind: tSpace, content: " "},
			{kind: tText, content: "vmo_create.md"},
			{kind: tNewline, content: "\n"},
			{kind: tEOF, content: ""},
		},
	},
}
