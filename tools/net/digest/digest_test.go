// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package digest

import (
	"testing"
)

var c = &credentials{
	username:  "admin",
	realm:     "Digest:4C1F0000000000000000000000000000",
	nonce:     "GZHoABAHAAAAAAAAtejSfCEQLbW+c/fM",
	uri:       "/index",
	algorithm: "MD5",
	qop:       "auth",
	method:    "POST",
	password:  "password",
}

var cnonce = "0a4f113b"

func TestHa1(t *testing.T) {
	r := c.ha1()
	if r != "e00fd2f74e4bb1ccd5c3f359e13822ce" {
		t.Fail()
	}
}

func TestHa2(t *testing.T) {
	r := c.ha2()
	if r != "f272ccec928f9de4e8e0bc6319ab2c66" {
		t.Fail()
	}
}

func TestResponse(t *testing.T) {
	r, err := c.response(cnonce)
	if err != nil {
		t.Fail()
	}
	if r != "ce25c065de2d1c900b21ed6d6fbe886b" {
		t.Fail()
	}
}
