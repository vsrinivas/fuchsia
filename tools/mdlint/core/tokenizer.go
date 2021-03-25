// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"strconv"
	"unicode"
)

type TokenKind int

const (
	_ TokenKind = iota

	Anchor
	Code
	EOF
	Header
	Link
	List
	Newline
	Space
	Text
	URL
)

var tokenKindStrings = map[TokenKind]string{
	Anchor:  "Anchor",
	Code:    "Code",
	EOF:     "EOF",
	Header:  "Header",
	Link:    "Link",
	List:    "List",
	Newline: "Newline",
	Space:   "Space",
	Text:    "Text",
	URL:     "URL",
}

func (kind TokenKind) String() string {
	if fmt, ok := tokenKindStrings[kind]; ok {
		return fmt
	}
	return fmt.Sprintf("tokenKind(%d)", kind)
}

// runesBufLen controls the size of the ring buffer backing readRune/unreadRune.
// It is sized to be sufficiently large to support unreading enough runes for
// the needs of the tokenizer.
const runesBufLen = 8

// A Doc represents a Markdown document.
//
// TODO(fxbug.dev/62964): Avoid duplicating a document's content between the
// content in the token, and the content in the doc. To support reading the
// content of a token while a line is being read, we need to either lookup the
// content in the accumulated lines, or in the accumulated line buffer.
type Doc struct {
	filename string

	stream *bufio.Reader

	// runeBuf is a ring buffer. Indices `[head, tail)`` are available for
	// writing. Indices `[tail, head)`` contains previously read runes that can
	// be unread by moving `readAt` backwards.
	runesBuf           [runesBufLen]rune
	head, tail, readAt uint

	aEOF bool

	ln, col int
	buf     bytes.Buffer
	lines   []string
}

func newDoc(filename string, stream io.Reader) *Doc {
	return &Doc{
		filename: filename,
		stream:   bufio.NewReader(stream),
		ln:       1,
		col:      1,
	}
}

func (doc *Doc) readRune() (rune, error) {
	r, err, fromStream := func() (rune, error, bool) {
		if doc.readAt == doc.head {
			if doc.aEOF {
				return rune(0), io.EOF, false
			}
			r, _, err := doc.stream.ReadRune()
			if err != nil {
				if err == io.EOF {
					doc.aEOF = true
					return rune(0), io.EOF, true
				}
			}
			doc.runesBuf[doc.head] = r
			doc.head = (doc.head + 1) % runesBufLen
			doc.readAt = doc.head
			if doc.tail == doc.head {
				doc.tail = (doc.tail + 1) % runesBufLen
			}
			return r, nil, true
		}
		r := doc.runesBuf[doc.readAt]
		doc.readAt = (doc.readAt + 1) % runesBufLen
		return r, nil, false
	}()
	if err != nil {
		if err == io.EOF && fromStream {
			doc.lines = append(doc.lines, doc.buf.String())
			doc.buf.Reset()
		}
	} else {
		if fromStream {
			doc.buf.WriteRune(r)
		}
		if r == '\n' {
			doc.ln++
			doc.col = 1
			if fromStream {
				doc.lines = append(doc.lines, doc.buf.String())
				doc.buf.Reset()
			}
		} else {
			doc.col++
		}
	}
	return r, err
}

func (doc *Doc) peekRune(n int) ([]rune, error) {
	if n < 0 {
		return nil, fmt.Errorf("invalid peek n, was %d", n)
	}
	peek := make([]rune, n, n)
	for i := 0; i < n; i++ {
		r, err := doc.readRune()
		if err != nil {
			return nil, err
		}
		peek[i] = r
	}
	for i := 0; i < n; i++ {
		if err := doc.unreadRune(); err != nil {
			return nil, err
		}
	}
	return peek, nil
}

func (doc *Doc) unreadRune() error {
	if doc.tail == doc.readAt {
		return fmt.Errorf("attempting to unread past end of the buffer")
	}
	doc.readAt = (doc.readAt + runesBufLen - 1) % runesBufLen
	if doc.col == 1 {
		if doc.ln != 1 {
			doc.ln--
			doc.col = len(doc.lines[doc.ln-1])
		}
	} else {
		doc.col--
	}
	return nil
}

type Token struct {
	Doc     *Doc
	Kind    TokenKind
	Content string

	// Ln indicates the line number of the start of the token, starting at 1.
	Ln int

	// Col indicates the column number of the start of the token, in number of
	// runes, starting at 1.
	Col int
}

func (tok Token) String() string {
	return fmt.Sprintf("%s(%d:%d:%s)", tok.Kind, tok.Ln, tok.Col, strconv.Quote(tok.Content))
}

type tokenizer struct {
	doc *Doc

	buf     bytes.Buffer
	context struct {
		ln, col               int
		lastKind              TokenKind
		isHeaderLine          bool
		onlySpaceSinceNewline bool
		followingLink         bool
	}
}

func newTokenizer(doc *Doc) *tokenizer {
	t := &tokenizer{
		doc: doc,
	}
	t.updateContext(Newline)
	return t
}

func (t *tokenizer) updateLnCol() {
	t.context.ln = t.doc.ln
	t.context.col = t.doc.col
}

func (t *tokenizer) updateContext(kind TokenKind) {
	switch kind {
	case Newline:
		t.context.isHeaderLine = false
		t.context.onlySpaceSinceNewline = true
	case Header:
		t.context.isHeaderLine = true
	case Space:
		// nothing
	case Link:
		t.context.followingLink = true
	default:
		t.context.onlySpaceSinceNewline = false
		t.context.followingLink = false
	}

	t.context.lastKind = kind
}

func (t *tokenizer) readBuf() string {
	defer t.buf.Reset()
	return t.buf.String()
}

func (t *tokenizer) newToken(kind TokenKind) Token {
	content := t.readBuf()
	tok := Token{
		Doc:     t.doc,
		Kind:    kind,
		Content: content,
		Ln:      t.context.ln,
		Col:     t.context.col,
	}
	t.context.col += len(content)
	return tok
}

func (t *tokenizer) next() (Token, error) {
	t.updateLnCol()
	tok, err := func() (Token, error) {
		r, err := t.doc.readRune()
		if err != nil {
			if err == io.EOF {
				return t.newToken(EOF), nil
			}
			return Token{}, err
		}
		t.buf.WriteRune(r)
		if r == '\n' {
			return t.newToken(Newline), nil
		}
		if r == '[' {
			if err := t.readUntil(true, func(r rune) bool { return r != ']' }); err != nil {
				return Token{}, err
			}
			return t.newToken(Link), nil
		}
		if r == '(' && t.context.followingLink {
			if err := t.readUntil(true, func(r rune) bool { return r != ')' }); err != nil {
				return Token{}, err
			}
			return t.newToken(URL), nil
		}
		if r == '#' && t.context.lastKind == Newline {
			if err := t.readUntil(false, func(r rune) bool { return r == '#' }); err != nil {
				return Token{}, err
			}
			return t.newToken(Header), nil
		}
		if (r == '*' || r == '-') && t.context.onlySpaceSinceNewline {
			peek, err := t.doc.peekRune(1)
			if err != nil {
				return Token{}, err
			}
			if isSeparatorSpace(peek[0]) {
				return t.newToken(List), nil
			}
		}
		if r == '{' && t.context.isHeaderLine {
			if err := t.readUntil(true, func(r rune) bool { return r != '}' }); err != nil {
				return Token{}, err
			}
			return t.newToken(Anchor), nil
		}
		if r == '`' {
			peek, err := t.doc.peekRune(2)
			if err != nil {
				return Token{}, err
			}
			if peek[0] == '`' && peek[1] == '`' {
				t.buf.WriteString("``")
				for i := 0; i < 2; i++ {
					if _, err := t.doc.readRune(); err != nil {
						panic("peek(2) followed by read failed; something is off")
					}
				}
				var ticksReadBefore int
				if err := t.readUntil(true, func(r rune) bool {
					if r == '`' {
						if ticksReadBefore == 2 {
							return false
						}
						ticksReadBefore++
					} else {
						ticksReadBefore = 0
					}
					return true
				}); err != nil {
					return Token{}, err
				}
				return t.newToken(Code), nil
			}
			var nextStopDone bool
			if err := t.readUntil(false, func(r rune) bool {
				if nextStopDone {
					return false
				}
				if r == '`' {
					nextStopDone = true
				}
				return true
			}); err != nil {
				return Token{}, err
			}
			return t.newToken(Code), nil
		}
		if isSeparatorSpace(r) {
			if err := t.readUntil(false, isSeparatorSpace); err != nil {
				return Token{}, err
			}
			return t.newToken(Space), nil
		}

		t.readUntil(false, func(r rune) bool { return !isSeparatorText(r) })
		return t.newToken(Text), nil
	}()
	if err != nil {
		return Token{}, err
	}
	t.updateContext(tok.Kind)
	return tok, nil
}

func (t *tokenizer) readUntil(includeLast bool, shouldContinue func(rune) bool) error {
	for {
		r, err := t.doc.readRune()
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return err
		}
		if ok := shouldContinue(r); !ok {
			if includeLast {
				t.buf.WriteRune(r)
			} else {
				t.doc.unreadRune()
			}
			return nil
		}
		t.buf.WriteRune(r)
	}
}

func isSeparatorSpace(r rune) bool {
	return unicode.In(r, unicode.Zs, unicode.Cc) && r != '\n'
}

func isSeparatorText(r rune) bool {
	return unicode.In(r, unicode.Zs, unicode.Cc) || r == '`'
}
