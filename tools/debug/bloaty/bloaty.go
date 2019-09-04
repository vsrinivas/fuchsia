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
	"sort"
	"strings"
	"sync"
)

type bloatyOutput struct {
	data row
	file string
	err  error
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
	fmt.Printf("running: %s\n", file)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		out <- bloatyOutput{err: fmt.Errorf("pipe: %s: %s", file, err)}
		return
	}

	var stderr bytes.Buffer
	cmd.Stderr = &stderr

	if err := cmd.Start(); err != nil {
		out <- bloatyOutput{err: fmt.Errorf("start (%s): %s: %s\n", err, file, cmd.Stderr)}
		stdout.Close()
		return
	}

	err = ReadCSV(stdout, out, file)
	if err != nil {
		out <- bloatyOutput{err: fmt.Errorf("csv: %s: %s", file, err)}
		stdout.Close()
		return
	}

	if err := cmd.Wait(); err != nil {
		out <- bloatyOutput{err: fmt.Errorf("wait (%s): %s: %s\n", err, file, cmd.Stderr)}
		return
	}
}

func updateSymbol(newSym *row, file string, sym *Symbol) {
	sym.Name = newSym.Symbol
	sym.Vmsz += newSym.Vmsz
	sym.Filesz += newSym.Filesz
	sym.Binaries = append(sym.Binaries, file)
}

func addRowToOutput(r *row, file string, output map[string]*Segment) {
	if _, ok := output[r.Seg]; !ok {
		output[r.Seg] = &Segment{make(map[string]*File)}
	}
	seg := output[r.Seg]

	if _, ok := seg.Files[r.File]; !ok {
		seg.Files[r.File] = &File{Symbols: make(map[string]*Symbol)}
	}
	f := seg.Files[r.File]

	if _, ok := f.Symbols[r.Symbol]; !ok {
		f.Symbols[r.Symbol] = &Symbol{}
	}
	updateSymbol(r, file, f.Symbols[r.Symbol])
	seg.Files[r.File] = f
	output[r.Seg] = seg
}

func getTopN(fileSizes map[string]uint64, topFiles, topSyms uint64, output *map[string]*Segment) {
	// If both topFiles and topSyms are 0, bail early because we're returning everything.
	if topFiles == 0 && topSyms == 0 {
		return
	}
	type sortedFile struct {
		name string
		size uint64
	}

	smallFiles := make(map[string]uint64)
	if topFiles > 0 && topFiles < uint64(len(fileSizes)) {
		var all []struct {
			name string
			size uint64
		}
		for name, size := range fileSizes {
			all = append(all, sortedFile{name, size})
		}
		sort.Slice(all, func(i, j int) bool {
			return all[i].size < all[j].size
		})

		for _, d := range all[:uint64(len(all))-topFiles] {
			smallFiles[d.name] = d.size
		}
	}

	for _, segData := range *output {
		smallFilesSize := uint64(0)
		for file, fileData := range segData.Files {
			smallSyms := Symbol{Name: "all small syms"}
			// If the file labeled a small file, add to small files size and delete the sym data.
			if size, exists := smallFiles[file]; exists {
				smallFilesSize += size
				delete(segData.Files, file)
			} else if topSyms > 0 && topSyms < uint64(len(fileData.Symbols)) {
				var all []*Symbol
				for _, sym := range fileData.Symbols {
					all = append(all, sym)
				}
				sort.Slice(all, func(i, j int) bool {
					return all[i].Filesz < all[j].Filesz
				})

				for _, d := range all[:uint64(len(all))-topSyms] {
					if sym, exists := fileData.Symbols[d.Name]; exists {
						smallSyms.Vmsz += sym.Vmsz
						smallSyms.Filesz += sym.Filesz
						delete(fileData.Symbols, d.Name)
					}
				}
			}

			if topSyms > 0 {
				fileData.Symbols["all small syms"] = &smallSyms
			}
		}

		if topFiles > 0 {
			segData.Files["all small files"] = &File{TotalFilesz: smallFilesSize}
		}
	}
}

// RunBloaty runs bloaty on all files in ids.txt, and returns a mapping of the
// symbols and files by segment.
func RunBloaty(bloatyPath, idsPath string, topFiles, topSyms uint64, jobs int) (map[string]*Segment, error) {
	files, err := getFiles(idsPath)
	if err != nil {
		return nil, err
	}

	var wg sync.WaitGroup
	data := make(chan bloatyOutput)
	output := make(map[string]*Segment)
	fileSizes := make(map[string]uint64)

	// Start up the data collection process.
	dataComplete := make(chan struct{}, 1)
	go func() {
		for d := range data {
			if d.err != nil {
				fmt.Printf("%v", d.err)
				continue
			}
			addRowToOutput(&d.data, d.file, output)
			fileSizes[d.data.File] += d.data.Filesz
		}
		dataComplete <- struct{}{}
	}()

	// Only allow up to max jobs concurrent executions.
	sem := make(chan struct{}, jobs)

	// Start a bloaty run on each file.
	for _, file := range files {
		wg.Add(1)
		sem <- struct{}{}
		go func(file string) {
			defer wg.Done()
			run(bloatyPath, file, data)
			<-sem
		}(file)
	}

	wg.Wait()
	close(data)
	<-dataComplete

	getTopN(fileSizes, topFiles, topSyms, &output)
	return output, nil
}
