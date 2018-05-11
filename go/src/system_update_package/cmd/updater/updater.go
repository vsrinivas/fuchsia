// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"os"
	"path/filepath"

	"system_update_package"
)

func main() {
	log.SetPrefix("system_updater: ")
	log.SetFlags(log.Ltime)

	pFile, err := os.Open(filepath.Join("/pkg", "data", "packages"))
	if err != nil {
		log.Fatalf("error opening packages data file! %s", err)
	}
	defer pFile.Close()

	iFile, err := os.Open(filepath.Join("/pkg", "data", "images"))
	if err != nil {
		log.Fatalf("error opening images data file! %s", err)
		return
	}
	defer iFile.Close()

	pkgs, imgs, err := system_update_package.ParseRequirements(pFile, iFile)
	if err != nil {
		log.Fatalf("could not parse requirements: %s", err)
	}

	amber, err := system_update_package.ConnectToUpdateSrvc()
	if err != nil {
		log.Fatalf("unable to connect to update service: %s", err)
	}

	if err := system_update_package.FetchPackages(pkgs, amber); err != nil {
		log.Fatalf("failed getting packages: %s", err)
	}

	if err := system_update_package.WriteImgs(imgs); err != nil {
		log.Fatalf("error writing image file: %s", err)
	}

	log.Println("system update complete, rebooting...")

	dmctl, err := os.OpenFile("/dev/misc/dmctl", os.O_RDWR, os.ModePerm)
	if err != nil {
		log.Printf("error forcing restart: %s", err)
	}
	defer dmctl.Close()
	cmd := []byte("reboot")
	if _, err := dmctl.Write(cmd); err != nil {
		log.Printf("error writing to control socket: %s", err)
	}
}
