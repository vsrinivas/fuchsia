// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package noticetxt

import (
	"bytes"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var notice = []byte(`package1
This is a fake license.

It's not real and doesn't apply to anything.
But it has multiple lines for testing purposes.

=================
package2

This is also a fake license. It has an empty line at the beginning.

=================

`)

func TestParseNoticeTxtFile(t *testing.T) {
	r := bytes.NewReader(notice)
	want := [][]byte{
		[]byte(`This is a fake license.

It's not real and doesn't apply to anything.
But it has multiple lines for testing purposes.

`),
		[]byte(`
This is also a fake license. It has an empty line at the beginning.

`),
	}
	got, err := parseNoticeTxtFile(r)
	if err != nil {
		t.Errorf("parseNoticeTxtFile(_) = _, %v; want _, nil", err)
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Errorf("parseNoticeTxtFile(_) returned unexpected diff (-want +got):\n%s", diff)
	}
}
