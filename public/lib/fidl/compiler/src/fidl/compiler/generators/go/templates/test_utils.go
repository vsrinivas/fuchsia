// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"bytes"
	"fmt"
	"go/format"
	"strings"
	"testing"
)

func check(t *testing.T, expected string, template string, input interface{}) {
	out, err := format.Source([]byte(expected))
	if err != nil {
		t.Fatalf("formatting expected results failed: %v: %s", err, expected)
	}
	expected = string(out)

	buf := new(bytes.Buffer)
	if err := goFileTmpl.ExecuteTemplate(buf, template, input); err != nil {
		t.Fatalf("template execution error: %v: on source %s", err, template)
	}

	out, err = format.Source(buf.Bytes())
	if err != nil {
		t.Fatalf("Formatting failed: %s\n%s\n", err, buf.Bytes())
	}
	actual := string(out)

	if expected != actual {
		errorMsg := fmt.Sprintf("Failed check: Expected\n%s\nActual\n%s\n", expected, actual)
		if strings.TrimSpace(expected) == strings.TrimSpace(actual) {
			errorMsg = fmt.Sprintf("%s\nTrailing or leading spaces differ.\n", errorMsg)
		}
		t.Fatalf(errorMsg)
	}
}
