// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amberd

import (
	"flag"
	"fmt"
	"log"
	"os"
	"runtime"
	"strings"
	"syscall/zx"
	"syscall/zx/fidl"

	"amber/control_server"
	"amber/daemon"
	"amber/metrics"
	"amber/source"

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
		usage = "usage: amber [-s=<path>]"
		store = flag.String("s", "/data/amber/store", "The path to the local file store")
	)

	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}

	ctx := context.CreateFromStartupInfo()

	{
		if l, err := syslog.NewLoggerWithDefaults(ctx.Connector(), "amber"); err != nil {
			log.Println(err)
		} else {
			syslog.SetDefaultLogger(l)
			log.SetOutput(&syslog.Writer{Logger: l})
		}
		log.SetFlags(0)
	}

	metrics.Register(ctx)

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
	d, err := daemon.NewDaemon(*store, source.PkgfsDir{"/pkgfs"}, &evtSvc)
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

	ctlSvr := control_server.NewControlServer(d)
	ctx.OutgoingService.AddService(amber.ControlName, func(c zx.Channel) error {
		_, err := ctlSvc.Add(ctlSvr, c, nil)
		return err
	})
	ctx.OutgoingService.AddService(amber.EventsName, func(c zx.Channel) error {
		_, err := evtSvc.Add(control_server.EventsImpl{}, c, nil)
		return err
	})

	for i := 1; i < runtime.NumCPU(); i++ {
		go fidl.Serve()
	}
	fidl.Serve()
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
