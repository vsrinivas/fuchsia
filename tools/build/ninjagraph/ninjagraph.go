// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ninjagraph provides utilities to parse the DOT output from Ninja's `-t graph`
// tool to a Go native format.
package ninjagraph

import (
	"bufio"
	"io"
	"regexp"
	"runtime"
	"strconv"
	"sync"

	"golang.org/x/sync/errgroup"
)

var (
	// Example: "0x12345678" [label="../../path/to/myfile.cc"]
	nodePattern = regexp.MustCompile(`^"0x([0-9a-f"]+)" \[label="([^"]+)"`)
	// Example: "0x12345678" -> "0xdeadbeef"
	edgePattern = regexp.MustCompile(`^"0x([0-9a-f]+)" -> "0x([0-9a-f]+)"`)
)

// Graph describes a Ninja build.
type Graph struct {
	// NodeToPath maps from node IDs to the paths of their corresponding inputs/outputs.
	NodeToPath map[int64]string
	// Edges maps from source node IDs to their destination Node IDs.
	Edges map[int64][]int64
}

// FromDOT reads all lines from the input reader and parses it into a `Graph`.
//
// This function expects the content to be parsed to follow the Ninja log format,
// otherwise the results is undefined.
func FromDOT(r io.Reader) (Graph, error) {
	s := bufio.NewScanner(r)

	g := Graph{
		NodeToPath: make(map[int64]string),
		Edges:      make(map[int64][]int64),
	}

	type node struct {
		id   int64
		path string
	}

	type edge struct {
		from, to int64
	}

	// Since we don't know the size of the file being parsed, we can't determine
	// a large enough size for buffered channels. So we use 2 unbuffered channels
	// and 2 goroutines to collect the results to the result graph.
	nodes := make(chan []node)
	edges := make(chan []edge)
	wg := sync.WaitGroup{}
	go func() {
		wg.Add(1)
		defer wg.Done()
		for ns := range nodes {
			for _, n := range ns {
				g.NodeToPath[n.id] = n.path
			}
		}
	}()
	go func() {
		wg.Add(1)
		defer wg.Done()
		for es := range edges {
			for _, e := range es {
				g.Edges[e.from] = append(g.Edges[e.from], e.to)
			}
		}
	}()

	// Use the rest of the cores to parse the file.
	eg := errgroup.Group{}
	sem := make(chan struct{}, runtime.GOMAXPROCS(0)-2)
	for s.Scan() {
		sem <- struct{}{}

		// Chunk the lines to reduce channel IO.
		lines := []string{s.Text()}
		for i := 0; i < 10_000 && s.Scan(); i++ {
			lines = append(lines, s.Text())
		}

		eg.Go(func() error {
			defer func() { <-sem }()

			var ns []node
			var es []edge

			for _, line := range lines {
				if match := nodePattern.FindStringSubmatch(line); match != nil {
					id, err := strconv.ParseInt(match[1], 16, 64)
					if err != nil {
						return err
					}
					ns = append(ns, node{id: id, path: match[2]})
					continue
				}

				if match := edgePattern.FindStringSubmatch(line); match != nil {
					from, err := strconv.ParseInt(match[1], 16, 64)
					if err != nil {
						return err
					}
					to, err := strconv.ParseInt(match[2], 16, 64)
					if err != nil {
						return err
					}
					es = append(es, edge{from: from, to: to})
					continue
				}
			}

			nodes <- ns
			edges <- es
			return nil
		})
	}

	if err := eg.Wait(); err != nil {
		return Graph{}, err
	}
	close(nodes)
	close(edges)
	wg.Wait()

	if err := s.Err(); err != nil && err != io.EOF {
		return Graph{}, err
	}
	return g, nil
}
