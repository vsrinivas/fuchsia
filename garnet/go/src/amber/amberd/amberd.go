// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amberd

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"syscall/zx"
	"syscall/zx/fidl"

	"amber/control_server"
	"amber/daemon"
	"amber/metrics"
	"amber/source"
	"amber/sys_update"

	"fidl/fuchsia/amber"

	"app/context"
	"syslog"
)

const (
	defaultSourceDir = "/system/data/amber/sources"
)

func Main() {

	var (
		// TODO(jmatt) replace hard-coded values with something better/more flexible
		usage      = "usage: amber [-k=<path>] [-s=<path>] [-u=<url>]"
		store      = flag.String("s", "/data/amber/store", "The path to the local file store")
		autoUpdate = flag.Bool("a", false, "Automatically update and restart the system as updates become available")
	)

	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}

	ctx := context.CreateFromStartupInfo()
	registerLogger(ctx)

	metrics.Register(ctx)

	readExtraFlags()

	flag.Parse()

	// The source dir is where we store our database of sources. Because we
	// don't currently have a mechanism to run "post-install" scripts,
	// we'll use the existence of the data dir to signify if we need to
	// load in the default sources.
	storeExists, err := exists(*store)
	if err != nil {
		log.Fatal(err)
	}

	var ctlSvc amber.ControlService
	var evtSvc amber.EventsService
	d, err := daemon.NewDaemon(*store, "", "", "", &evtSvc)
	if err != nil {
		log.Fatalf("failed to start daemon: %s", err)
	}

	// Now that the daemon is up and running, we can register all of the
	// system configured sources, if they exist.
	//
	// TODO(etryzelaar): Since these sources are only installed once,
	// there's currently no way to upgrade them. PKG-82 is tracking coming
	// up with a plan to address this.
	if !storeExists {
		defaultConfigsExist, err := exists(defaultSourceDir)
		if err != nil {
			log.Fatal(err)
		}

		if defaultConfigsExist {
			log.Printf("initializing store: %s", *store)
			if err := addDefaultSourceConfigs(d, defaultSourceDir); err != nil {
				log.Fatalf("failed to register default sources: %s", err)
			}
		}
	}

	supMon := sys_update.NewSystemUpdateMonitor(*autoUpdate, d)
	ctlSvr := control_server.NewControlServer(d, supMon)
	ctx.OutgoingService.AddService(amber.ControlName, func(c zx.Channel) error {
		_, err := ctlSvc.Add(ctlSvr, c, nil)
		return err
	})
	ctx.OutgoingService.AddService(amber.EventsName, func(c zx.Channel) error {
		_, err := evtSvc.Add(control_server.EventsImpl{}, c, nil)
		return err
	})

	go func() {
		log.Printf("monitoring for updates")
		supMon.Start()
		log.Println("system update monitor exited")
	}()

	for i := 1; i < runtime.NumCPU(); i++ {
		go fidl.Serve()
	}
	fidl.Serve()
}

type logWriter struct{}

func (l *logWriter) Write(data []byte) (n int, err error) {
	origLen := len(data)

	// Strip out the trailing newline the `log` library adds because the
	// logging service also adds a trailing newline.
	if len(data) > 0 && data[len(data)-1] == '\n' {
		data = data[:len(data)-1]
	}

	if err := syslog.Infof("%s", data); err != nil {
		return 0, err
	}

	return origLen, nil
}

func registerLogger(ctx *context.Context) {
	if err := syslog.InitDefaultLoggerWithTags(ctx.Connector(), "amber"); err != nil {
		log.Printf("error initializing syslog interface: %s", err)
	}
	log.SetOutput(&logWriter{})
	log.SetFlags(0)
}

// addDefaultSourceConfigs installs source configs from a directory.
// The directory structure looks like:
//
//     $dir/source1/config.json
//     $dir/source2/config.json
//     ...
func addDefaultSourceConfigs(d *daemon.Daemon, dir string) error {
	configs, err := source.LoadSourceConfigs(dir)
	if err != nil {
		return err
	}

	var errs []string
	for _, cfg := range configs {
		if err := d.AddSource(cfg); err != nil {
			errs = append(errs, err.Error())
		}
	}

	if len(errs) == 0 {
		return nil
	}
	return fmt.Errorf("error adding default configs: %s", strings.Join(errs, ", "))
}

var flagsDir = filepath.Join("/system", "data", "amber", "flags")

func readExtraFlags() {
	d, err := os.Open(flagsDir)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("unexpected error reading %q: %s", flagsDir, err)
		}
		return
	}
	defer d.Close()

	files, err := d.Readdir(0)
	if err != nil {
		log.Printf("error listing flags directory %s", err)
		return
	}
	for _, f := range files {
		if f.IsDir() || f.Size() == 0 {
			continue
		}

		fPath := filepath.Join(d.Name(), f.Name())
		file, err := os.Open(fPath)
		if err != nil {
			log.Printf("flags file %q could not be opened: %s", fPath, err)
			continue
		}
		r := bufio.NewReader(file)
		for {
			line, err := r.ReadString('\n')
			if err != nil && err != io.EOF {
				log.Printf("flags file %q had read error: %s", fPath, err)
				break
			}

			line = strings.TrimSpace(line)
			os.Args = append(os.Args, line)
			if err == io.EOF {
				break
			}
		}
		file.Close()
	}
}

// Check if a path exists.
func exists(path string) (bool, error) {
	if _, err := os.Stat(path); err != nil {
		if os.IsNotExist(err) {
			return false, nil
		} else {
			return false, err
		}
	}

	return true, nil
}
