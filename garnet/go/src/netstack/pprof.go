// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//+build pprof

package netstack

import (
	"log"
	"net/http"
	_ "net/http/pprof"
)

func init() {
	go func() {
		log.Println("starting http pprof server on port 6060")
		log.Println(http.ListenAndServe(":6060", nil))
	}()
}
