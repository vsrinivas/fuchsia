// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fmt"
	"strconv"
	"strings"
	"testing"
)

type xrefDef struct {
	xref, href string
}

type recognizeLinksTestCase struct {
	ignore string

	input    string
	xrefUses []string
	hrefUses []string
	xrefDefs []xrefDef
	tocs     []string
}

type recognizerAcc struct {
	DefaultLintRuleOverPatterns

	xrefUses []string
	hrefUses []string
	xrefDefs []xrefDef
	tocs     []string
}

var _ LintRuleOverPatterns = (*recognizerAcc)(nil)

func (r *recognizerAcc) OnLinkByXref(xref Token) {
	r.xrefUses = append(r.xrefUses, xref.Content)
}

func (r *recognizerAcc) OnLinkByURL(href Token) {
	r.hrefUses = append(r.hrefUses, href.Content)
}

func (r *recognizerAcc) OnXrefDefinition(xref, href Token) {
	r.xrefDefs = append(r.xrefDefs, xrefDef{xref.Content, href.Content})
}

func (r *recognizerAcc) OnTableOfContents(toc Token) {
	r.tocs = append(r.tocs, toc.Content)
}

func TestRecognizeLinks(t *testing.T) {
	cases := []recognizeLinksTestCase{
		{
			input: "before [link-and-xref] after [another-link-and-xref].",
			xrefUses: []string{
				"[link-and-xref]",
				"[another-link-and-xref]",
			},
		},
		{
			input: "something [link-and-xref]\n content continues",
			xrefUses: []string{
				"[link-and-xref]",
			},
		},
		{
			input: "something [link][xref]\n content continues",
			xrefUses: []string{
				"[xref]",
			},
		},
		{
			input: "something [link] : (not-a-href)",
			xrefUses: []string{
				"[link]",
			},
		},
		{
			input: "something [link] (this-is-a-href)",
			hrefUses: []string{
				"(this-is-a-href)",
			},
		},
		{
			input: "something [link] \n  (this-is-a-href)\n[link2]\n(href2!)",
			hrefUses: []string{
				"(this-is-a-href)",
				"(href2!)",
			},
		},
		{
			input: "[link]: this-is-a-href",
			xrefDefs: []xrefDef{
				{"[link]", "this-is-a-href"},
			},
		},
		{
			input: "\n\n [link]  :this-is-a-href",
			xrefDefs: []xrefDef{
				{"[link]", "this-is-a-href"},
			},
		},
		{
			input: "\n[xref]",
			xrefUses: []string{
				"[xref]",
			},
		},
		{
			input: "hello\n[TOC]\nhello\n  [toc]\nbut not [toc] here",
			xrefUses: []string{
				"[toc]",
			},
			tocs: []string{
				"[TOC]",
				"[toc]",
			},
		},
		{
			input: `## An example

		- [first]
		- [second]

		[first]: /first/url/here.md
		[second]: /second/url/here.md`,
			xrefUses: []string{
				"[first]",
				"[second]",
			},
			xrefDefs: []xrefDef{
				{"[first]", "/first/url/here.md"},
				{"[second]", "/second/url/here.md"},
			},
		},
		{
			input: "[link-and-xref1]\n[xref1]:def1\n\n\n[link-and-xref2]\n[xref2]:def2",
			xrefUses: []string{
				"[link-and-xref1]",
				"[link-and-xref2]",
			},
			xrefDefs: []xrefDef{
				{"[xref1]", "def1"},
				{"[xref2]", "def2"},
			},
		},
		{
			input: "[toc]\n[toc]",
			tocs: []string{
				"[toc]",
				"[toc]",
			},
		},
		{
			input: `
[this is a valid link][link label]

[link label]: http://google.com`,
			xrefUses: []string{
				"[link label]",
			},
			xrefDefs: []xrefDef{
				{"[link label]", "http://google.com"},
			},
		},
		{
			input: "Linux:  [`/proc/[pid]/stat`](https://man7.org/linux/man-pages/man5/procfs.5.html)",
			hrefUses: []string{
				"(https://man7.org/linux/man-pages/man5/procfs.5.html)",
			},
		},
		{
			ignore: "fxbug.dev/76515",
			input: `
For instance, [fidlgen_rust] [would like to know](http://fxbug.dev/61760)
whether a type may ever contain a floating point number to determine which...

[fidlgen_rust]: /some/url/here`,
			xrefUses: []string{
				"[fidlgen_rust]",
			},
			hrefUses: []string{
				"(http://fxbug.dev/61760)",
			},
			xrefDefs: []xrefDef{
				{"[fidlgen_rust]", "/some/url/here"},
			},
		},
	}
	for inputNum, ex := range cases {
		t.Run(fmt.Sprintf("input #%d", inputNum), func(t *testing.T) {
			if len(ex.ignore) != 0 {
				return
			}
			t.Log(strconv.Quote(ex.input))
			var (
				acc       = &recognizerAcc{}
				r         = recognizer{rule: acc}
				tokenizer = newTokenizer(newDoc("filename", strings.NewReader(ex.input)))
				tok       Token
				err       error
			)
			for tok.Kind != EOF {
				tok, err = tokenizer.next()
				if err != nil {
					t.Fatalf("unexpected tokenization error: %s", err)
				}
				r.OnNext(tok)
			}
			// xrefUses
			if len(ex.xrefUses) != len(acc.xrefUses) {
				t.Errorf("len(xrefUses): expected=%d, actual=%d", len(ex.xrefUses), len(acc.xrefUses))
			} else {
				for i := 0; i < len(ex.xrefUses); i++ {
					if acc.xrefUses[i] != ex.xrefUses[i] {
						t.Errorf("xrefUses[%d]: expected=%s, actual=%s", i, ex.xrefUses[i], acc.xrefUses[i])
					}
				}
			}
			// hrefUses
			if len(ex.hrefUses) != len(acc.hrefUses) {
				t.Errorf("len(hrefUses): expected=%d, actual=%d", len(ex.hrefUses), len(acc.hrefUses))
			} else {
				for i := 0; i < len(ex.hrefUses); i++ {
					if acc.hrefUses[i] != ex.hrefUses[i] {
						t.Errorf("hrefUses[%d]: expected=%s, actual=%s", i, ex.hrefUses[i], acc.hrefUses[i])
					}
				}
			}
			// xrefDefs
			if len(ex.xrefDefs) != len(acc.xrefDefs) {
				t.Errorf("len(xrefDefs): expected=%d, actual=%d", len(ex.xrefDefs), len(acc.xrefDefs))
			} else {
				for i := 0; i < len(ex.xrefDefs); i++ {
					if acc.xrefDefs[i].xref != ex.xrefDefs[i].xref {
						t.Errorf("xrefDefs[%d]: expected.xref=%s, actual.xref=%s", i, ex.xrefDefs[i].xref, acc.xrefDefs[i].xref)
					}
					if acc.xrefDefs[i].href != ex.xrefDefs[i].href {
						t.Errorf("xrefDefs[%d]: expected.href=%s, actual.href=%s", i, ex.xrefDefs[i].href, acc.xrefDefs[i].href)
					}
				}
			}
		})
	}
}
