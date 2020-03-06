// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

var alphabet []rune

func init() {
	add := func(start, end rune) {
		for letter := start; letter <= end; letter++ {
			alphabet = append(alphabet, letter)
		}
	}
	add('0', '9')
	add('A', 'Z')
	add('a', 'z')
	alphabet = append(alphabet, '_', ' ', '\n', ';')
}

type operation interface {
	apply(input []byte) []byte
}

type insertOp struct {
	pos    int
	letter rune
}

func (op insertOp) apply(input []byte) []byte {
	var b bytes.Buffer
	b.Write(input[0:op.pos])
	b.WriteRune(op.letter)
	b.Write(input[op.pos:])
	return b.Bytes()
}

func (op *insertOp) String() string {
	return fmt.Sprintf("%+v", *op)
}

type removeOp struct {
	pos int
}

func (op removeOp) apply(input []byte) []byte {
	var b bytes.Buffer
	b.Write(input[0:op.pos])
	b.Write(input[op.pos+1:])
	return b.Bytes()
}

type changeOp struct {
	pos    int
	letter rune
}

func (op changeOp) apply(input []byte) []byte {
	var b bytes.Buffer
	b.Write(input[0:op.pos])
	b.WriteRune(op.letter)
	b.Write(input[op.pos+1:])
	return b.Bytes()
}

var _ = []operation{
	(*insertOp)(nil),
	(*removeOp)(nil),
	(*changeOp)(nil),
}

type dataAndDistance struct {
	data     []byte
	distance int
}

func (input dataAndDistance) apply(op operation) dataAndDistance {
	return dataAndDistance{
		op.apply(input.data),
		input.distance + 1,
	}
}

func genNewInputs(input dataAndDistance, outAllInputs chan dataAndDistance) {
	// insertOp
	for pos := 0; pos <= len(input.data); pos++ {
		for _, letter := range alphabet {
			outAllInputs <- input.apply(&insertOp{pos, letter})
		}
	}

	// removeOp, changeOp
	for pos := 0; pos < len(input.data); pos++ {
		outAllInputs <- input.apply(&removeOp{pos})
		for _, letter := range alphabet {
			outAllInputs <- input.apply(&changeOp{pos, letter})
		}
	}
}

type fidlcResult struct {
	stderr []byte
	err    error
}

type fidlcRunner string

func (path fidlcRunner) run(content []byte) fidlcResult {
	tmpfile, err := ioutil.TempFile("", "example")
	if err != nil {
		panic(err)
	}
	defer os.Remove(tmpfile.Name())
	if _, err := tmpfile.Write(content); err != nil {
		panic(err)
	}
	if err := tmpfile.Close(); err != nil {
		panic(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, string(path), "--files", tmpfile.Name())
	stderr, err := cmd.StderrPipe()
	if err != nil {
		panic(err)
	}
	if err := cmd.Start(); err != nil {
		panic(err)
	}
	stderrData, err := ioutil.ReadAll(stderr)
	if err != nil {
		panic(err)
	}
	return fidlcResult{stderrData, cmd.Wait()}
}

var extractError = regexp.MustCompile(`[0-9]+:[0-9]+: error: (.*)`)

var errorCategories = map[*regexp.Regexp]int{
	regexp.MustCompile(`^unexpected identifier .*, was expecting .*`): 1,
	regexp.MustCompile(`^invalid character .*`):                       1,
	regexp.MustCompile(`^invalid identifier .*`):                      1,
	regexp.MustCompile(`^Invalid library name component .*`):          1,
	regexp.MustCompile(`^unexpected token .*, was expecting .*`):      1,
	regexp.MustCompile(`^unknown type .*`):                            1,
	regexp.MustCompile(`^cannot specify strictness for .*`):           1,
	regexp.MustCompile(`^Multiple struct fields with the same name;`): 1,
}

type fidlcRunAnalyser struct {
	runs       int
	hardExits  int
	unknownErr int

	// accumulated points
	errPoints map[*regexp.Regexp]int
}

func (a fidlcRunAnalyser) String() string {
	var parts []string
	for re, points := range a.errPoints {
		parts = append(parts, fmt.Sprintf("%s: %d", re, points))
	}
	sort.Strings(parts)
	return fmt.Sprintf("runs: %d, hardExits: %d, unknownErr %d\n%s",
		a.runs, a.hardExits, a.unknownErr,
		strings.Join(parts, "\n"))
}

func (a *fidlcRunAnalyser) analyse(result fidlcResult) {
	a.runs++

	if result.err == nil {
		return
	}

	if _, ok := (result.err).(*exec.ExitError); !ok {
		a.hardExits++
		return
	}

	submatches := extractError.FindAllSubmatch(result.stderr, -1)
	for _, matches := range submatches {
		if len(matches) < 2 {
			// TODO: capture this, and analyse it better. We should never reach this
			// line.
			a.unknownErr++
			return
		}
		match := matches[1]
		for re, points := range errorCategories {
			if re.Match(match) {
				a.errPoints[re] = a.errPoints[re] + points
				return
			}
		}
		fmt.Printf("unknownErr '%s'\n", string(match))
		a.unknownErr++
	}
}

// numParallelFidlc configures the number of maximum number of parallel
// invocations of fidlc.
const numParallelFidlc = 10

// baseLibrary is the library which gets modified successively to generate
// adjacent libraries, as measured by their levenstein distance.
const baseLibrary = `library lib;struct Color {uint16 r;uint16 g;uint16 b;};`

var fidlcPath = flag.String("fidlc", "", "relative path to FIDL compiler (fidlc).")

func main() {
	flag.Parse()

	if !flag.Parsed() || fidlcPath == nil {
		flag.PrintDefaults()
		os.Exit(1)
	}

	var (
		inputs  = make(chan dataAndDistance)
		results = make(chan fidlcResult)
	)

	// bootstrap: read base.fidl as first input
	firstInput := dataAndDistance{[]byte(baseLibrary), 0}

	// single goroutine: generate lots of inputs
	go func() {
		inputs <- firstInput
		genNewInputs(firstInput, inputs)
		close(inputs)
	}()

	// multiple goroutines: process inputs into results
	//
	// (We need a wait group to close the results channel when all processing
	// goroutines have completed.)
	var wg sync.WaitGroup
	fidlc := fidlcRunner(*fidlcPath)
	for i := 0; i < numParallelFidlc; i++ {
		wg.Add(1)
		go func() {
			for input := range inputs {
				results <- fidlc.run(input.data)
			}
			wg.Done()
		}()
	}
	go func() {
		wg.Wait()
		close(results)
	}()

	analyzer := fidlcRunAnalyser{
		errPoints: make(map[*regexp.Regexp]int),
	}

	for result := range results {
		analyzer.analyse(result)
		if analyzer.runs%1000 == 0 {
			fmt.Printf("%s\n\n", analyzer)
		}
	}
	fmt.Printf("%s\n\n", analyzer)
}
