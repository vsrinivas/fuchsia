// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestBadHeaders(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"dupe_header.md": `
# Header

## Summary
Duplicate header below!
«##» Summary
Duplicate header above!`,
			"trip_header.md": `
# Header

## Summary
Duplicate header below!
«##» Summary
Duplicate header above, triplicate header below!
«##» Summary
Triplicate header above!`,
			"dupe_at_eof.md": `
# Header

## Summary
Duplicate header in final line (i.e. followed by EOF, not newline) should still be caught.
«##» Summary`,
			"trip_at_eof.md": `
# Header

## Summary
Duplicate header below!
«##» Summary
Duplicate header above, triplicate header below!
«##» Summary`,
		},
	}.runOverTokens(t, newBadHeaders)
}

func TestBadHeaders_siblingHeaders(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"trip_sibling_header.md": `
# Header

## Section A
### Introduction

## Section B
The header below has no duplicate siblings, although it does have duplicate 'cousins'; this is acceptable.
### Introduction

## Section C
### Introduction
The header below is a duplicate of it's sibling one line above; this is unacceptable.
«###» Introduction

## Section D
### Section D1
#### Introduction
### Section D2
#### Introduction
«####» Introduction`,
		},
	}.runOverTokens(t, newBadHeaders)
}

func TestBadHeaders_headersIgnoreCaseAndWhitespace(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"ignore_whitespace.md": `
# Header

## Duplicate
«##»  Duplicate
## Not a duplicate
«##»   Dupli  cat e `,
			"ignore_case": `
# Header

## Introduction
«##» INTRODUCTION
«##» iNtRoDuCtIoN
## Intro`,
			"ignore_both_case_and_whitespace": `
# Header

## Duplicate
«##» Du Pli Cate
«##»  Dup LiCaTe
«##»    DuplicatE  `,
		},
	}.runOverTokens(t, newBadHeaders)
}

func TestBadHeaders_misnumberedHeaders(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"misnumbered_headers.md": `
# Title A
## Title A
«####» Title A
Above header is two levels higher than previous header, making it invalid. Only H2, or H3 would be valid here.

### Title B
#### Title B
«##» Title B
Above header is tow levels lower than previous header, making it invalid. Only H3, H4, or H5 would be valid here.`,
		},
	}.runOverTokens(t, newBadHeaders)
}

func TestBadHeaders_initialHeaderAtTopOfDoc(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"h1_at_top.md": `
# Header`,
			"h1_preceded_by_jinja.md": `
{% set a = 0 %}
{% set b = 1 %}
{# comment #}
# Header`,
			"h1_preceded_by_text.md": `
«This» is text before a header, only the first token is reported to avoid spamming.
# Header
`,
			"h1_preceded_by_jinja_and_text.md": `
{# This comment is acceptable #}
«This» is text before a header, and it is unacceptable.
{# This comment is acceptable #}
# Header`,
			"h1_preceded_by_html_comment.md": `
<!-- This is an HTML comment, which is allowed to precede H1 -->
# Header`,
			"h1_preceded_by_html_comment_jinja_and_text.md": `
<!-- This HTML comment is acceptable -->
{# This jinja comment is acceptable #}
«This» is text before a header, and it is unacceptable.
# Header`,
		},
	}.runOverTokens(t, newBadHeaders)
}
