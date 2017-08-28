// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"crypto/sha512"
	"encoding/hex"
	"flag"
	"fmt"
	"hash"
	"io"
	"log"
	"net/url"
	"os"
	"path/filepath"
	"time"

	"amber/daemon"
	"amber/source"

	tuf "github.com/flynn/go-tuf/client"
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
	needsPath = "/pkgfs/needs"
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

	keys, err := source.LoadKeys(*keys)
	if err != nil {
		log.Printf("loading root keys failed %s\n", err)
		return
	}

	client, _, err := source.InitNewTUFClient(*addr, *store, keys)
	if err != nil {
		log.Printf("client initialization failed: %s\n", err)
	}

	if err != nil {
		fmt.Println(err)
		os.Exit(2)
	}

	d := startupDaemon(client, *addr)
	log.Println("amber: monitoring for updates")
	defer d.CancelAll()

	//block forever
	select {}
}

func doDemo() {
	if err := os.MkdirAll(needsPath, os.ModePerm); err != nil {
		fmt.Printf("Error making needs dir\n")
		return
	}

	f, err := os.Create(filepath.Join(needsPath, demoNeed))
	if err != nil {
		fmt.Printf("Error making needs file %s\n", err)
		return
	}
	f.Close()

	// sleep a moment for the daemon to see the file
	time.Sleep(1 * time.Second)
	os.Remove(filepath.Join(needsPath, demoNeed))
}

func startupDaemon(client *tuf.Client, srvAddr string) *daemon.Daemon {
	files := []string{
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

	// create source with 5 qps rate limit
	fetcher := &daemon.TUFSource{
		Client:   client,
		Interval: time.Millisecond * 200,
	}
	checker := daemon.NewDaemon(reqSet, daemon.ProcessPackage)
	checker.AddSource(fetcher)
	u, err := url.Parse(srvAddr)
	if err == nil {
		u.Path = filepath.Join(u.Path, "blobs")
		checker.AddBlobRepo(daemon.BlobRepo{Address: u.String(), Interval: time.Second * 5})
	} else {
		log.Printf("amber: bad blob repo address %s\n", err)
	}
	pmMonitor(checker)
	return checker
}

func pmMonitor(d *daemon.Daemon) {
	go daemon.NewWatcher(d).Watch(needsPath)
}

func digest(name string, hash hash.Hash) ([]byte, error) {
	f, e := os.Open(name)
	if e != nil {
		fmt.Printf("amber: couldn't open file to fingerprint %s\n", e)
		return nil, e
	}
	defer f.Close()

	if _, err := io.Copy(hash, f); err != nil {
		fmt.Printf("amber: file digest failed %s\n", err)
		return nil, e
	}
	return hash.Sum(nil), nil
}
