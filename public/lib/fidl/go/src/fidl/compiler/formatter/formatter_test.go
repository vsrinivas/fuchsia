// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package formatter

import (
	"strings"
	"testing"
)

func TestFormatMojom(t *testing.T) {
	original := `// Top line comment.
[Attr1=1,
 Attr2=2]
/* left comment */ module hello.world;  // Right comment

// First block line 1
// First block line 2

// Second block line 1
// Second block line 2
import "foo1.mojom";
import "foo2.mojom";

import "afoo1.mojom";
import "afoo2.mojom";

[Attr1=1,
 Attr2=2]
struct FooStruct {  // FooStruct comment.
  // Field 1 comment.
  int8 field1;  // Field 1 comment.
  int16 field2 = 10;
  // Field3 comment.
	// Field3 other comment.
  int32 field3@2 = 10;
};

struct FooUnion {
	[Attr1=1] int8 field1;
	int16 field2@1;
};

enum FooEnum {
	VALUE1,  // VALUE1 comment.
	VALUE2 = 10,  // VALUE2 comment.

  // FooEnum Final Comment.
};

// no-format

enum SomeWeirdFormat { VALUE1 = 10, VALUE2 = 20};




// end-no-format

interface InterfaceBar {
  CreateApplicationConnector(
      ApplicationConnector& application_connector_request);
  SetRootScene(SceneToken scene_token,
               uint32 scene_version,
               mojo.Rect viewport);
};

interface CommentOnly {
	// Only a comment.
};

// constant comment.
const int8 foo_constant1 = 10;  // constant comment.
const int8 foo_constant2 = -10;  // constant comment.
const float foo_constant3 = -10e10;  // constant comment.
const int64 foo_constant4 = 0xAD10;  // constant comment.
const int64 foo_constant5 = -0xAD10;  // constant comment.

// Interface Comment.
interface InterfaceFoo {  // Interface comment.
	const int8 const_in_interface = 20;

  // Method 1 comment.
  method1@5(int8 hello@0);
  // Method 2 comment.
	method2([MinVersion=5] int8 hello) => (Foo bar@0);
	method3();
	method4() => (Foo bar);
	method5(int8 p1 /* p1 comment */, int16 p2);  // method comment
	method6(WayTooLongAndReallyLongFactoryVisitoryFactory field1,
	        WayTooLongAndReallyLongFactoryVisitoryFactory field2)
	    => (int8 alpha);
};

// Final Comments.
`

	// TODO(azani): Remove this and just fix the tabs.
	original = strings.Replace(original, "\t", "  ", -1)
	original = strings.Replace(original, "\t//", "  //", -1)

	actual, err := FormatMojom("test.mojom", original)
	if err != nil {
		t.Fatalf("Parser was not supposed to fail: %s", err.Error())
	}

	if original != actual {
		t.Fatalf("\nExpected:\n%v\n\n*****\n\nActual:\n%v eof", original, actual)
	}
}

func TestFormatNoExtraFinalBlankLine(t *testing.T) {
	original := `interface ImagePipe {
  // Some comment.
  FlushImages();

};
`

	expected := `interface ImagePipe {
  // Some comment.
  FlushImages();
};
`
	actual, err := FormatMojom("test.mojom", original)
	if err != nil {
		t.Fatalf("Parser was not supposed to fail: %s", err.Error())
	}

	if expected != actual {
		t.Fatalf("\nExpected:\n%v\n\n*****\n\nActual:\n%v eof", expected, actual)
	}
}
