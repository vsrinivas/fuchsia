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
	"log"
	"os"
	"path/filepath"
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
	delay = flag.Duration("d", 0*time.Second, "Set a delay before Amber does its work")
	demo  = flag.Bool("demo", false, "run demo mode, writes an entry to /data/pkgs/needs")

	demoNeed  = "lib-usb-audio.so"
	needsPath = "/data/pkgs/needs"
)

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}

	flag.Parse()
	log.SetPrefix("amber: ")
	log.SetFlags(log.Ltime)

	time.Sleep(*delay)

	if *demo {
		doDemo()
		return
	}

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

	d := startupDaemon(client)
	defer d.CancelAll()

	//block forever
	select {}
}

func doDemo() {
	if err := os.MkdirAll(needsPath, os.ModePerm); err != nil {
		fmt.Printf("Error making needs dir %v\n")
		return
	}

	f, err := os.Create(filepath.Join(needsPath, demoNeed))
	if err != nil {
		fmt.Printf("Error making needs file %v\n", err)
		return
	}
	f.Close()

	// sleep a moment for the daemon to see the file
	time.Sleep(1 * time.Second)
	os.Remove(filepath.Join(needsPath, demoNeed))
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

func startupDaemon(client *tuf.Client) *daemon.Daemon {
	if err := os.MkdirAll(daemon.UpdateDst, os.ModePerm); err != nil {
		// TODO(jmatt) retry for some time period?
		fmt.Printf("Error creating update destination directory %v\n", err)
	}

	files := []string{
		"/system/apps/amber"}
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
	checker.AddSource(fetcher)
	pmMonitor(checker)
	return checker
}

func pmMonitor(d *daemon.Daemon) error {
	s := time.NewTicker(200 * time.Millisecond)
	l := time.NewTicker(5 * time.Minute)
	end := make(chan struct{})
	go daemon.WatchNeeds(d, s.C, l.C, end, needsPath)
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
