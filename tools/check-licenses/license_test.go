// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"testing"
)

func TestParseAuthor(t *testing.T) {
	data := []string{
		"Copyright (C) 2020 Foo All rights reserved",
		"Copyright © 2020 Foo All rights reserved",
		"Copyright © 2020 Foo",
	}
	for _, in := range data {
		if m := parseAuthor(in); m != "Foo" {
			t.Errorf("%q failed, got %q", in, m)
		}
	}
}

func TestGetAuthorMatches(t *testing.T) {
	data := []string{
		"Copyright (C) 2020 Foo All rights reserved",
		"Copyright © 2020 Foo All rights reserved",
		"Copyright © 2020 Foo",
	}
	for _, in := range data {
		if m := getAuthorMatches([]byte(in)); len(m) == 0 {
			t.Errorf("%q failed, got %q", in, m)
		}
	}
}
