// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
)

func mainImpl() error {
	static := flag.String("static", "", "path to a static .textproto")

	flag.Parse()

	fmt.Println("static:", *static)

	bytes, err := ioutil.ReadFile(*static)
	if err != nil {
		return err
	}

	if _, err := parseStatic(string(bytes)); err != nil {
		return err
	}

	return nil
}

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "fint: %s\n", err)
		os.Exit(1)
	}
}
