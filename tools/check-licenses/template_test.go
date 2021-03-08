// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"fmt"
	"testing"
)

func TestGetHTMLTextDoesNotEscapeBR(t *testing.T) {
	tests := []struct {
		text string
		want string
	}{
		{
			text: "nothing to do",
			want: "nothing to do",
		},
		{
			text: "text with a\nlinebreak",
			want: "text with a<br />linebreak",
		},
		{
			text: "text with a <br /> tag to escape",
			want: "text with a &lt;br /&gt; tag to escape",
		},
		{
			text: "text\nwith <br /> both",
			want: "text<br />with &lt;br /&gt; both",
		},
	}
	for _, tc := range tests {
		m := &Match{Text: tc.text}
		fmt.Printf("%v", m)
		getHTMLText := getFuncMap(nil)["getHTMLText"].(func(*Match) string)
		if got := getHTMLText(m); got != tc.want {
			t.Errorf("getHTMLText(%q) = %q; want %q", m.Text, got, tc.want)
		}
	}
}
