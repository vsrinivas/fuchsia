// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"crypto/sha512"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"hash"
	"io"
	"os"
	"time"

	"fuchsia.googlesource.com/amber/daemon"
	tuf "github.com/flynn/go-tuf/client"
	"github.com/flynn/go-tuf/data"
)

var (
	// TODO(jmatt) replace hard-coded values with something better/more flexible
	usage = "usage: amber [-k=<path>] [-s=<path>] [-u=<url>]"
	store = flag.String("s", "/data/amber/tuf", "The path to the local file store")
	addr  = flag.String("u", "http://192.168.3.1:8083", "The URL (including port if not using port 80)  of the update server.")
	keys  = flag.String("k", "/system/data/amber/keys", "Path to use to initialize the client's keys. This is only needed the first time the command is run.")
)

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}

	flag.Parse()

	tufStore, err := tuf.FileLocalStore(*store)
	if err != nil {
		fmt.Printf("amber: couldn't open datastore %v\n", err)
		return
	}

	server, err := tuf.HTTPRemoteStore(*addr, nil)
	if err != nil {
		fmt.Printf("amber: couldn't understand server address %v\n", err)
		return
	}

	client := tuf.NewClient(tufStore, server)

	doInit, err := needsInit(tufStore)
	if doInit {
		rootKeys, err := loadKeys(*keys)
		if err != nil {
			fmt.Printf("amber: please provide keys for client %v\n", err)
			return
		}
		err = client.Init(rootKeys, len(rootKeys))
		if err != nil {
			fmt.Printf("amber: client failed to init with provided keys %v\n", err)
			return
		}
	}

	if err != nil {
		fmt.Println(err)
		os.Exit(2)
	}

	amber(client)
}

func needsInit(store tuf.LocalStore) (bool, error) {
	meta, err := store.GetMeta()
	if err != nil {
		return false, err
	}

	_, ok := meta["root.json"]
	if !ok {
		return true, nil
	}

	return false, nil
}

func loadKeys(path string) ([]*data.Key, error) {
	f, err := os.Open(path)
	defer f.Close()
	if err != nil {
		return nil, err
	}

	var keys []*data.Key
	err = json.NewDecoder(f).Decode(&keys)
	return keys, err
}

func amber(client *tuf.Client) error {
	files := []string{"/system/bin/tuf-client",
		"/system/bin/amber"}
	reqSet := daemon.NewPackageSet()

	d := sha512.New()
	// get the current SHA512 hash of the file
	for _, name := range files {
		sha, err := digest(name, d)
		d.Reset()
		if err != nil {
			continue
		}

		hexStr := hex.EncodeToString(sha)
		pkg := daemon.Package{Name: name, Version: hexStr}
		reqSet.Add(&pkg)
	}

	fetcher := &daemon.TUFSource{Client: client, Interval: time.Second * 5}
	checker := daemon.NewDaemon(reqSet, daemon.ProcessPackage)
	defer checker.CancelAll()
	checker.AddSource(fetcher)

	fmt.Println("Press Ctrl+C or Ctrl+D to exit.")
	buf := make([]byte, 1)
	for {
		_, err := os.Stdin.Read(buf)
		if (err != nil && err != io.EOF) || buf[0] == 3 || buf[0] == 4 {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}

	return nil
}

func digest(name string, hash hash.Hash) ([]byte, error) {
	f, e := os.Open(name)
	if e != nil {
		fmt.Printf("amber: couldn't open file to fingerprint %v\n", e)
		return nil, e
	}
	defer f.Close()

	if _, err := io.Copy(hash, f); err != nil {
		fmt.Printf("amber: file digest failed %v\n", err)
		return nil, e
	}
	return hash.Sum(nil), nil
}
