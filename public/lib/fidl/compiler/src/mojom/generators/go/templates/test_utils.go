// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"bytes"
	"fmt"
	"log"
	"strings"
	"testing"

	"mojom/generators/go/gofmt"
)

func check(t *testing.T, expected string, template string, input interface{}) {
	buffer := &bytes.Buffer{}
	if err := goFileTmpl.ExecuteTemplate(buffer, template, input); err != nil {
		t.Fatalf("Template execution error(%s):%s\n", template, err)
	}

	expected, err := gofmt.FormatFragment(expected)
	if err != nil {
		log.Panicf("Formatting error (expected): %s\n", err)
	}

	src := buffer.String()
	actual, err := gofmt.FormatFragment(src)
	if err != nil {
		t.Fatalf("Formatting failed: %s\n%s\n", err, src)
	}

	if expected != actual {
		errorMsg := fmt.Sprintf("Failed check: Expected\n%s\nActual\n%s\n", expected, actual)
		if strings.TrimSpace(expected) == strings.TrimSpace(actual) {
			errorMsg = fmt.Sprintf("%s\nTrailing or leading spaces differ.\n", errorMsg)
		}
		t.Fatalf(errorMsg)
	}
}
