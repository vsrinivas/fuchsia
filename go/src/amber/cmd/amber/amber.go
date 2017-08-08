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
	"net/url"
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
			log.Printf("amber: please provide keys for client %v\n", err)
			return
		}

		err = initClient(client, rootKeys)
		if err != nil {
			log.Println("client initialization failed, exiting")
		}
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

func initClient(client *tuf.Client, keys []*data.Key) error {
	log.Println("initializing client")
	delay := 1 * time.Second
	maxStep := 30 * time.Second
	maxDelay := 5 * time.Minute

	// TODO(jmatt) consider giving up?
	for {
		err := client.Init(keys, len(keys))
		if err == nil {
			break
		}
		log.Println("client failed to init with provided keys")
		time.Sleep(delay)

		if delay > maxStep {
			delay += maxStep
		} else {
			delay += delay
		}

		if delay > maxDelay {
			delay = maxDelay
		}
	}
	return nil
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

func startupDaemon(client *tuf.Client, srvAddr string) *daemon.Daemon {
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

	fetcher := &daemon.TUFSource{Client: client, Interval: time.Minute * 5}
	checker := daemon.NewDaemon(reqSet, daemon.ProcessPackage)
	checker.AddSource(fetcher)
	u, err := url.Parse(srvAddr)
	if err == nil {
		u.Path = filepath.Join(u.Path, "blobs")
		checker.AddBlobRepo(daemon.BlobRepo{Address: u.String(), Interval: time.Second * 5})
	} else {
		log.Printf("amber: bad blob repo address %v\n", err)
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
