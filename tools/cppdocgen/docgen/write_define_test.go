// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"bytes"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"testing"
)

func TestWriteDefine(t *testing.T) {
	settings := WriteSettings{
		LibName:            "libtest",
		BuildRelSourceRoot: "../..",
		BuildRelIncludeDir: "../../src",
		RepoBaseUrl:        "https://example.com/main/",
	}

	// Our header name should end up as "lib/test/myheader.h"
	headerPath := "../../src/lib/test/myheader.h"

	d := Define{
		Location: clangdoc.Location{99, headerPath},
		Name:     "FOO",
		Value:    "(5)",
		Description: []clangdoc.CommentInfo{
			{
				Kind: "TextComment",
				Text: "This is the documentation for the thing.",
			},
		},
	}

	index := makeEmptyIndex()

	out := bytes.Buffer{}
	writeDefineReference(settings, &index, d, &out)
	expected :=
		`## FOO macro {:#FOO}

[Declaration source code](https://example.com/main/src/lib/test/myheader.h#99)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">FOO</span> (5)
</pre>

This is the documentation for the thing.
`
	if out.String() != expected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), expected)
	}

	// Do one with empty arguments
	d.ParamString = "()"
	out = bytes.Buffer{}
	writeDefineReference(settings, &index, d, &out)
	expected =
		`## FOO() macro {:#FOO}

[Declaration source code](https://example.com/main/src/lib/test/myheader.h#99)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">FOO</span>() (5)
</pre>

This is the documentation for the thing.
`
	if out.String() != expected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), expected)
	}

	// Nonempty arguments (title gets "...").
	d.ParamString = "(a, b)"
	out = bytes.Buffer{}
	writeDefineReference(settings, &index, d, &out)
	expected =
		`## FOO(…) macro {:#FOO}

[Declaration source code](https://example.com/main/src/lib/test/myheader.h#99)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">FOO</span>(a, b) (5)
</pre>

This is the documentation for the thing.
`
	if out.String() != expected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), expected)
	}
}
