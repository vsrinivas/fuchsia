// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"bytes"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"testing"
)

func TestWriteRecordDeclarationBlock(t *testing.T) {
	r := &clangdoc.RecordInfo{
		Name:    "AwesomeSauce",
		USR:     "Record_USR",
		Path:    "ns/Saucemaster",
		TagType: "Class",
		Namespace: []clangdoc.Reference{
			// Note the order is most specific first (the reverse of C++).
			{Type: "Record", Name: "Saucemaster"},
			{Type: "Namespace", Name: "ns"},
			{Type: "Namespace", Name: "outer"},
			// Clang-doc generates "GlobalNamespace" references in some cases and these
			// should be skipped.
			{Type: "Namespace", Name: "GlobalNamespace"},
		},
	}

	index := makeEmptyIndex()
	index.RecordUsrs[r.USR] = r

	out := bytes.Buffer{}
	writeRecordDeclarationBlock(&index, r, []clangdoc.MemberTypeInfo{}, &out)
	expected :=
		`<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> outer {
<span class="kwd">namespace</span> ns {

<span class="kwd">class</span> <span class="typ">Saucemaster::AwesomeSauce</span> { <span class="com">...</span> };

}  <span class="com">// namespace ns</span>
}  <span class="com">// namespace outer</span>
</pre>

`
	if out.String() != expected {
		t.Errorf("Got: %s\nExpected: %s\n", out.String(), expected)
	}
}
