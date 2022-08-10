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
	settings := WriteSettings{LibName: "libtest",
		StripPathEltCount: 3,
		RepoBaseUrl:       "http://example.com/main/"}

	index := makeEmptyIndex()

	// With "StripPathEltCount == 3 and this input string, our header name should end
	// up as "lib/test/myheader.h"
	headerPath := "../../src/lib/test/myheader.h"

	// Define a function.
	fn := &clangdoc.FunctionInfo{Name: "MyFunction",
		ReturnType: clangdoc.ReturnType{Type: clangdoc.Type{Name: "void"}}}
	index.Functions["USR_FOR_FUNCTION"] = fn

	// Define a class.
	rec := &clangdoc.RecordInfo{
		Name:        "MyClass",
		Path:        "",
		TagType:     "Class",
		DefLocation: clangdoc.Location{99, headerPath}}
	index.Records["USR_FOR_RECORD"] = rec

	header := &Header{Name: headerPath,
		Functions: []*clangdoc.FunctionInfo{fn},
		Records:   []*clangdoc.RecordInfo{rec}}
	index.Headers[headerPath] = header

	out := bytes.Buffer{}
	WriteHeaderReference(settings, &index, header, &out)

	headerExpected := `# \<lib/test/myheader.h\> in libtest

[Header source code](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/test/myheader.h)

## Class MyClass {:#}

[Declaration source code](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/test/myheader.h#99)

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
