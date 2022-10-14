// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"bytes"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"strings"
	"testing"
)

func TestWriteHeaderReference(t *testing.T) {
	settings := WriteSettings{
		LibName:            "libtest",
		BuildRelSourceRoot: "../..",
		BuildRelIncludeDir: "../../src",
		RepoBaseUrl:        "http://example.com/main/"}

	index := makeEmptyIndex()

	// Our header name should end up as "lib/test/myheader.h"
	headerPath := "../../src/lib/test/myheader.h"

	// Define a function.
	fn := &clangdoc.FunctionInfo{
		Name:       "MyFunction",
		USR:        "SomeUSR",
		ReturnType: clangdoc.ReturnType{Type: clangdoc.Type{Name: "void"}}}
	index.FunctionUsrs[fn.USR] = fn
	index.FunctionNames[fn.Name] = fn

	// Define a class.
	rec := &clangdoc.RecordInfo{
		Name:        "MyClass",
		Path:        "",
		TagType:     "Class",
		DefLocation: clangdoc.Location{99, headerPath}}
	index.RecordUsrs["USR_FOR_RECORD"] = rec

	header := &Header{Name: headerPath,
		Functions: []*clangdoc.FunctionInfo{fn},
		FunctionGroups: []*FunctionGroup{
			{Funcs: []*clangdoc.FunctionInfo{fn}},
		},
		Records: []*clangdoc.RecordInfo{rec}}
	index.Headers[headerPath] = header

	out := bytes.Buffer{}
	WriteHeaderReference(settings, &index, header, &out)

	headerExpected := `# \<lib/test/myheader.h\> in libtest

[Header source code](http://example.com/main/src/lib/test/myheader.h)

## MyClass class {:#}

[Declaration source code](http://example.com/main/src/lib/test/myheader.h#99)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">class</span> <span class="typ">MyClass</span> { <span class="com">...</span> };
</pre>

## MyFunction() {:#MyFunction}

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> <b>MyFunction</b>();
</pre>`
	headerGot := strings.TrimSpace(out.String())
	if headerGot != headerExpected {
		t.Errorf("Got:\n%s\n\nExpected:\n%s\n", headerGot, headerExpected)
	}
}

func TestWriteHeaderComment(t *testing.T) {
	settings := WriteSettings{
		LibName:            "libtest",
		BuildRelSourceRoot: "../..",
		BuildRelIncludeDir: "../../src",
		RepoBaseUrl:        "http://example.com/main/"}

	index := makeEmptyIndex()

	// Our header name should end up as "lib/test/myheader.h"
	headerPath := "../../src/lib/test/myheader.h"

	// This header has a header comment but no custom title.
	header := &Header{
		Name: headerPath,
		Description: []clangdoc.CommentInfo{
			{Kind: "TextComment",
				Text: " First line text",
			},
			{Kind: "TextComment",
				Text: "",
			},
			{Kind: "TextComment",
				Text: " Contents text",
			},
		},
		Functions: []*clangdoc.FunctionInfo{},
		Records:   []*clangdoc.RecordInfo{}}
	index.Headers[headerPath] = header

	out := bytes.Buffer{}
	WriteHeaderReference(settings, &index, header, &out)

	headerExpected := `# \<lib/test/myheader.h\> in libtest

[Header source code](http://example.com/main/src/lib/test/myheader.h)

First line text

Contents text`
	headerGot := strings.TrimSpace(out.String())
	if headerGot != headerExpected {
		t.Errorf("Got:\n%s\n\nExpected:\n%s\n", headerGot, headerExpected)
	}

	// Set a custom title in the header comment.
	header.Description[0].Text = " # My custom title"
	out = bytes.Buffer{}
	WriteHeaderReference(settings, &index, header, &out)

	headerExpected = `# My custom title

[Header source code](http://example.com/main/src/lib/test/myheader.h)


Contents text`
	headerGot = strings.TrimSpace(out.String())
	if headerGot != headerExpected {
		t.Errorf("Got:\n%s\n\nExpected:\n%s\n", headerGot, headerExpected)
	}
}
