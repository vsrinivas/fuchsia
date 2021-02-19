// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"strconv"
	"unicode"
)

type tokenKind int

const (
	_ tokenKind = iota

	tAnchor
	tCode
	tEOF
	tHeader
	tLink
	tList
	tNewline
	tSpace
	tText
	tURL
)

var tokenKindStrings = map[tokenKind]string{
	tAnchor:  "tAnchor",
	tCode:    "tCode",
	tEOF:     "tEOF",
	tHeader:  "tHeader",
	tLink:    "tLink",
	tList:    "tList",
	tNewline: "tNewline",
	tSpace:   "tSpace",
	tText:    "tText",
	tURL:     "tURL",
}

func (kind tokenKind) String() string {
	if fmt, ok := tokenKindStrings[kind]; ok {
		return fmt
	}
	return fmt.Sprintf("tokenKind(%d)", kind)
}

// runesBufLen controls the size of the ring buffer backing readRune/unreadRune.
// It is sized to be sufficiently large to support unreading enough runes for
// the needs of the tokenizer.
const runesBufLen = 8

// A doc represents a Markdown document.
//
// TODO(fxbug.dev/62964): Avoid duplicating a document's content between the
// content in the token, and the content in the doc. To support reading the
// content of a token while a line is being read, we need to either lookup the
// content in the accumulated lines, or in the accumulated line buffer.
type doc struct {
	filename string

	stream *bufio.Reader

	// runeBuf is a ring buffer. Indices `[head, tail)`` are available for
	// writing. Indices `[tail, head)`` contains previously read runes that can
	// be unread by moving `readAt` backwards.
	runesBuf           [runesBufLen]rune
	head, tail, readAt uint

	atEOF bool

	ln, col int
	buf     bytes.Buffer
	lines   []string
}

func newDoc(filename string, stream io.Reader) *doc {
	return &doc{
		filename: filename,
		stream:   bufio.NewReader(stream),
		ln:       1,
		col:      1,
	}
}

func (doc *doc) readRune() (rune, error) {
	r, err, fromStream := func() (rune, error, bool) {
		if doc.readAt == doc.head {
			if doc.atEOF {
				return rune(0), io.EOF, false
			}
			r, _, err := doc.stream.ReadRune()
			if err != nil {
				if err == io.EOF {
					doc.atEOF = true
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

func (doc *doc) peekRune(n int) ([]rune, error) {
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

func (doc *doc) unreadRune() error {
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

type token struct {
	doc     *doc
	kind    tokenKind
	content string

	// ln, col are 1-based
	ln, col int
}

func (tok token) String() string {
	return fmt.Sprintf("%s(%d:%d:%s)", tok.kind, tok.ln, tok.col, strconv.Quote(tok.content))
}

type tokenizer struct {
	doc *doc

	buf     bytes.Buffer
	context struct {
		ln, col               int
		lastKind              tokenKind
		isHeaderLine          bool
		onlySpaceSinceNewline bool
		followingLink         bool
	}
}

func newTokenizer(doc *doc) *tokenizer {
	t := &tokenizer{
		doc: doc,
	}
	t.updateContext(tNewline)
	return t
}

func (t *tokenizer) updateLnCol() {
	t.context.ln = t.doc.ln
	t.context.col = t.doc.col
}

func (t *tokenizer) updateContext(kind tokenKind) {
	switch kind {
	case tNewline:
		t.context.isHeaderLine = false
		t.context.onlySpaceSinceNewline = true
	case tHeader:
		t.context.isHeaderLine = true
	case tSpace:
		// nothing
	case tLink:
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

func (t *tokenizer) newToken(kind tokenKind) token {
	content := t.readBuf()
	tok := token{
		doc:     t.doc,
		kind:    kind,
		content: content,
		ln:      t.context.ln,
		col:     t.context.col,
	}
	t.context.col += len(content)
	return tok
}

func (t *tokenizer) next() (token, error) {
	t.updateLnCol()
	tok, err := func() (token, error) {
		r, err := t.doc.readRune()
		if err != nil {
			if err == io.EOF {
				return t.newToken(tEOF), nil
			}
			return token{}, err
		}
		t.buf.WriteRune(r)
		if r == '\n' {
			return t.newToken(tNewline), nil
		}
		if r == '[' {
			if err := t.readUntil(true, func(r rune) bool { return r != ']' }); err != nil {
				return token{}, err
			}
			return t.newToken(tLink), nil
		}
		if r == '(' && t.context.followingLink {
			if err := t.readUntil(true, func(r rune) bool { return r != ')' }); err != nil {
				return token{}, err
			}
			return t.newToken(tURL), nil
		}
		if r == '#' && t.context.lastKind == tNewline {
			if err := t.readUntil(false, func(r rune) bool { return r == '#' }); err != nil {
				return token{}, err
			}
			return t.newToken(tHeader), nil
		}
		if (r == '*' || r == '-') && t.context.onlySpaceSinceNewline {
			peek, err := t.doc.peekRune(1)
			if err != nil {
				return token{}, err
			}
			if isSeparatorSpace(peek[0]) {
				return t.newToken(tList), nil
			}
		}
		if r == '{' && t.context.isHeaderLine {
			if err := t.readUntil(true, func(r rune) bool { return r != '}' }); err != nil {
				return token{}, err
			}
			return t.newToken(tAnchor), nil
		}
		if r == '`' {
			peek, err := t.doc.peekRune(2)
			if err != nil {
				return token{}, err
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
					return token{}, err
				}
				return t.newToken(tCode), nil
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
				return token{}, err
			}
			return t.newToken(tCode), nil
		}
		if isSeparatorSpace(r) {
			if err := t.readUntil(false, isSeparatorSpace); err != nil {
				return token{}, err
			}
			return t.newToken(tSpace), nil
		}

		t.readUntil(false, func(r rune) bool { return !isSeparatorText(r) })
		return t.newToken(tText), nil
	}()
	if err != nil {
		return token{}, err
	}
	t.updateContext(tok.kind)
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
