// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Update repository revisions in the manifest.
package main

import (
	"bufio"
	"encoding/xml"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

type stringsValue []string

func (i *stringsValue) String() string {
	return strings.Join(*i, ",")
}

func (i *stringsValue) Set(value string) error {
	*i = strings.Split(value, ",")
	return nil
}

var (
	manifestVar string
	projectsVar = stringsValue{}
)

func init() {
	flag.StringVar(&manifestVar, "manifest", "", "Name of the manifest file")
	flag.Var(&projectsVar, "projects", "List of projects to update")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: update-manifest\n")
		flag.PrintDefaults()
	}
}

type Manifest struct {
	Projects []Project `xml:"projects>project"`
	XMLName  struct{}  `xml:"manifest"`
}

type Project struct {
	Name         string   `xml:"name,attr,omitempty"`
	Remote       string   `xml:"remote,attr,omitempty"`
	RemoteBranch string   `xml:"remotebranch,attr,omitempty"`
	Revision     string   `xml:"revision,attr,omitempty"`
	XMLName      struct{} `xml:"project"`
}

func manifestFromBytes(data []byte) (*Manifest, error) {
	m := new(Manifest)
	if err := xml.Unmarshal(data, m); err != nil {
		return nil, err
	}
	return m, nil
}

func getLatestRevision(manifest, remote, branch string) (string, error) {
	cmd := exec.Command("git", "ls-remote", remote, fmt.Sprintf("refs/heads/%s", branch))
	cmd.Dir = filepath.Dir(manifest)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return "", err
	}
	if err := cmd.Start(); err != nil {
		return "", err
	}
	r := bufio.NewReader(stdout)
	out, _, err := r.ReadLine()
	if err != nil {
		return "", err
	}
	if err := cmd.Wait(); err != nil {
		return "", err
	}
	return strings.Fields(string(out))[0], nil
}

func updateManifest(manifest string, projects map[string]bool) error {
	content, err := ioutil.ReadFile(filepath.Join(manifest))
	if err != nil {
		return fmt.Errorf("Could not read from %s: %s", manifest, err)
	}

	m, err := manifestFromBytes(content)
	if err != nil {
		return fmt.Errorf("Cannot parse manifest %s: %s", manifest, err)
	}

	str := string(content)
	for _, p := range m.Projects {
		if len(projects) > 0 {
			if _, ok := projects[p.Name]; !ok {
				continue
			}
		}
		if p.Revision != "" {
			branch := "master"
			if p.RemoteBranch != "" {
				branch = p.RemoteBranch
			}
			revision, err := getLatestRevision(manifest, p.Remote, branch)
			if err != nil {
				return err
			}
			str = strings.Replace(str, p.Revision, string(revision), 1)
		}
	}

	if err := ioutil.WriteFile(manifest, []byte(str), os.ModePerm); err != nil {
		return fmt.Errorf("Could not write to %s: %s", manifest, err)
	}
	return nil
}

func main() {
	flag.Parse()

	if _, err := os.Stat(manifestVar); os.IsNotExist(err) {
		log.Fatalf("Manifest %s does not exist", manifestVar)
	}

	projects := map[string]bool{}
	for _, p := range projectsVar {
		projects[p] = true
	}

	if err := updateManifest(manifestVar, projects); err != nil {
		log.Fatal(err)
	}
}
