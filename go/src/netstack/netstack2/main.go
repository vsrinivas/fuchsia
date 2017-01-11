// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "log"

func main() {
	log.SetFlags(0)
	log.SetPrefix("netstack2: ")
	log.Print("started")
	// TODO(crawshaw): everything
	log.Print("stopped")
}
