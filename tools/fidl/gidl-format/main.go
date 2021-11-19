// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"strings"
)

var inPlace = flag.Bool("i", false, "Formats file in place")

func usage() {
	out := flag.CommandLine.Output()
	fmt.Fprintf(out,
		`Usage: %s [-i] [<file>...]

Format GIDL syntax in each <file>, or from standard input if no files are given.

Options:
`, os.Args[0])
	flag.PrintDefaults()
}

func main() {
	// Disable timestamp in logs.
	log.SetFlags(0)
	flag.Usage = usage
	flag.Parse()
	if *inPlace && flag.NArg() == 0 {
		log.Fatal("-i not accepted when formatting standard input")
	}
	os.Exit(run())
}

// run is separate from main to avoid os.Exit skipping deferred calls.
func run() int {
	var dst io.StringWriter
	if !*inPlace {
		stdout := bufio.NewWriter(os.Stdout)
		defer stdout.Flush()
		dst = stdout
	}
	status := 0
	if flag.NArg() == 0 {
		if err := format(dst, os.Stdin, "<stdin>"); err != nil {
			log.Print(err)
			status = 1
		}
	} else {
		for _, filename := range flag.Args() {
			if err := process(dst, filename); err != nil {
				log.Print(err)
				status = 1
			}
		}
	}
	return status
}

// process reads from filename and formats it to dst.
// If dst is nil, it formats the file in place instead.
func process(dst io.StringWriter, filename string) error {
	flag := os.O_RDWR
	if dst != nil {
		flag = os.O_RDONLY
	}
	file, err := os.OpenFile(filename, flag, 0)
	if err != nil {
		return err
	}
	defer file.Close()
	// First case: write to dst.
	if dst != nil {
		return format(dst, file, filename)
	}
	// Second case: overwrite file, if anything changed.
	var b strings.Builder
	if err := format(&b, file, filename); err != nil {
		return err
	}
	stat, err := file.Stat()
	if err != nil {
		return err
	}
	if stat.Size() == int64(b.Len()) {
		file.Seek(0, 0)
		contents, err := io.ReadAll(file)
		if err != nil {
			return fmt.Errorf("%s: %w", filename, err)
		}
		if string(contents) == b.String() {
			// Nothing changed. Skip writing.
			return nil
		}
	}
	file.Truncate(0)
	file.Seek(0, 0)
	if _, err := file.WriteString(b.String()); err != nil {
		return fmt.Errorf("%s: %w", filename, err)
	}
	return nil
}
