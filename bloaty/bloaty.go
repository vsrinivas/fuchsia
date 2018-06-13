// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"bufio"
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"
)

type bloatyOutput struct {
	key  string
	data Symbol
	file string
	err  error
}

// Symbol represents all data about one symbol in the produced Bloaty output.
type Symbol struct {
	Name        string         `json:"Name"`
	File        string         `json:"File"`
	Segs        map[string]int `json:"Segs"`
	TotalVmsz   uint64         `json:"TotalVmsz"`
	TotalFilesz uint64         `json:"TotalFilesz"`
	Binaries    []string       `json:"Binaries"`
}

// TODO(jakehehrlich): Add reading ids.txt to elflib, since there are now three
// different tools that each need to read it for mostly unrelated reasons
func getFiles(idsPath string) ([]string, error) {
	out := []string{}
	idsFile, err := os.Open(idsPath)
	if err != nil {
		return nil, err
	}
	defer idsFile.Close()
	scanner := bufio.NewScanner(idsFile)
	for line := 1; scanner.Scan(); line++ {
		parts := strings.SplitN(scanner.Text(), " ", 2)
		if len(parts) != 2 {
			return nil, fmt.Errorf("invalid ids.txt: error on line %d", line)
		}
		out = append(out, parts[1])
	}
	return out, nil
}

func run(bloatyPath, file string, out chan<- bloatyOutput) {
	args := []string{
		"-d", "segments,compileunits,symbols",
		"-s", "file",
		"--tsv",
		"-n", "0",
		file,
	}
	cmd := exec.Command(bloatyPath, args...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		out <- bloatyOutput{err: fmt.Errorf("pipe: %s: %s", file, err)}
		return
	}
	var stderr bytes.Buffer
	cmd.Stderr = &stderr

	if err := cmd.Start(); err != nil {
		out <- bloatyOutput{err: fmt.Errorf("start: %s: %s", file, cmd.Stderr)}
		return
	}

	rows := make(chan row)
	go func() {
		err = ReadCSV(stdout, rows)
		if err != nil {
			out <- bloatyOutput{err: fmt.Errorf("csv: %s: %s", file, err)}
			return
		}
	}()

	for r := range rows {
		data := bloatyOutput{
			key: r.Symbol + ":" + r.File,
			data: Symbol{
				Name:        r.Symbol,
				File:        r.File,
				Segs:        make(map[string]int),
				TotalVmsz:   r.Vmsz,
				TotalFilesz: r.Filesz,
				Binaries:    append([]string{}, file),
			},
			file: file,
		}
		data.data.Segs[r.Seg] += 1
		out <- data
	}

	if err := cmd.Wait(); err != nil {
		out <- bloatyOutput{err: fmt.Errorf("wait: %s: %s", file, cmd.Stderr)}
		return
	}

}

func updateSymbol(sym, newSym Symbol, file string) Symbol {
	sym.Name = newSym.Name
	sym.File = newSym.File
	// TODO: Filtering by section would allow some of these symbols to be ignored
	// or considered on a more useful global level.
	for seg, count := range newSym.Segs {
		sym.Segs[seg] += count
	}
	sym.TotalVmsz += newSym.TotalVmsz
	sym.TotalFilesz += newSym.TotalFilesz
	sym.Binaries = append(sym.Binaries, file)
	return sym
}

func RunBloaty(bloatyPath, idsPath string) (map[string]Symbol, error) {
	files, err := getFiles(idsPath)
	if err != nil {
		return nil, err
	}

	var wg sync.WaitGroup
	output := make(map[string]Symbol)
	data := make(chan bloatyOutput)

	for _, file := range files {
		wg.Add(1)
		go func(bloatyPath, file string) {
			run(bloatyPath, file, data)
			wg.Done()
		}(bloatyPath, file)
	}

	go func() {
		wg.Wait()
		close(data)
	}()

	for d := range data {
		if d.err != nil {
			fmt.Printf("error: %v", d.err)
			continue
		}
		if sym, ok := output[d.key]; !ok {
			output[d.key] = d.data
		} else {
			output[d.key] = updateSymbol(sym, d.data, d.file)
		}
	}

	return output, nil
}
