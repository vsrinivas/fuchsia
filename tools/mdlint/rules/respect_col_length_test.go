// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestRespectColLength(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
this line however is exactly eighty characters long, so all should be well right
but then, we go over eighty characters by just a hair and will get a warning «here»

Now, this line is too long!! It goes to beyond ninety characters and that is bad «but» only one token should be reported.

Now, this line is too long!! It goes to beyond ninety characters and that is bad         
but since what is over is space, we do not report it.`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_longWords(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
We have a really-long-word exemption:

Sometimeswordsarejustreallylongandcannotbebrokenuplikesupercalifragilisticexpiali
Sometimeswordsarejustreallylongandcannotbebrokenuplikesupercalifragilisticexpialidocious

It also applies to lists:

 * indent
   * indent
     * indent
       * indent
         * indent
           * indent
             * This-long-word-is-allowed-even-though-it-starts-on-column-16-not-on-column-1
             * But in this sentence we do NOT subtract 16, so it must not go «past» 80

Or here:

1234567890. This-long-word-is-allowed-even-though-it-starts-on-column-13-not-on-column-1

1234567890. But in this sentence we do NOT subtract 13, so it must not go past «80»
`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_longWordsCont(t *testing.T) {
	// TODO(fxbug.dev/62964): See https://spec.commonmark.org/0.29/#block-quotes
	t.Skip("block quotes are not yet recognized, when they are, combine with previous test")

	ruleTestCase{
		files: map[string]string{
			"example.md": `
And block quotes:

> Blockquotessometimeswordsarejustreallylongandcannotbebrokenuplikesupercalifragi

   > Blockquotessometimeswordsarejustreallylongandcannotbebrokenuplikesupercalifragilisticexpialidocious

(We test these in decreasing order of 'countStartingAtCol' to ensure it is reset
properly.)
`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_indentedCodeBlock(t *testing.T) {
	// TODO(fxbug.dev/62964): See https://spec.commonmark.org/0.29/#indented-code-blocks
	t.Skip("code blocks are not yet recognized, when they are, combine with next test")

	ruleTestCase{
		files: map[string]string{
			"example.md": `
We relax this rule for code blocks:

    code code code code code code code code code code code code code code code code code
`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_fencedCodeBlock(t *testing.T) {
	tripleBacktick := "```"
	ruleTestCase{
		files: map[string]string{
			"example.md": `
And fenced blocks:

` + tripleBacktick + `
code code code code code code code code code code code code code code code code code
` + tripleBacktick,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_multiLineToken(t *testing.T) {
	backtick := "`"
	ruleTestCase{
		files: map[string]string{
			"example.md": `
This line is long long long long long long long long long long long [But it
only] exceeds the maximum if you count "only]".

This line is long long long long long long long long long long long ` + backtick + `But it
only` + backtick + ` exceeds the maximum if you count "only(backtick)".
`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_longLinkDestination(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
word word word word word word word word word word word word word word, [link
link link](https://example.com/00000000000000000000000000000000000000000000000000000000000000000000000000000000000000)`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_longLinkReferenceDefinition(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
See [foo] and [bar] for details.

[foo]: https://example.com/00000000000000000000000000000000000000000000000000000000000000000000000000000000000000
[bar]: /docs/path/path/path/path/path/path/path/path/path/path/path/path/path/path/path/path/path/path/to/file.md`,
		},
	}.runOverTokens(t, newRespectColLength)
}

func TestRespectColLength_longTitles(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
when we have titles

### Alternative: Remember if unknown fields were discarded {#alternative-remember}

we cannot complain about length. We should complain again later, like here «because» we're over.`,
		},
	}.runOverTokens(t, newRespectColLength)
}
