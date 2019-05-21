// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/base64"
	"encoding/xml"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
)

const fuchsiaURL = "https://fuchsia.googlesource.com"

type giStatus string

const (
	giStatusUnknown giStatus = "UNKNOWN"
	giStatusPassed           = "PASSED"
	giStatusPending          = "PENDING"
)

func downloadGIManifest(name string) ([]byte, error) {
	u, err := url.Parse(fuchsiaURL)
	if err != nil {
		return nil, err
	}
	u.Path = "/integration/+/refs/heads/master/" + name
	q := u.Query()
	q.Add("format", "TEXT")
	u.RawQuery = q.Encode()

	resp, err := http.Get(u.String())
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	return base64.StdEncoding.DecodeString(string(b))
}

// Structs for unmarshalling the jiri manifest XML data.
type manifest struct {
	XMLName  xml.Name  `xml:"manifest"`
	Projects []project `xml:"projects>project"`
}
type project struct {
	XMLName  xml.Name `xml:"project"`
	Name     string   `xml:"name,attr"`
	Revision string   `xml:"revision,attr"`
}

func getGIRevision(content []byte, project string) (string, error) {
	m := manifest{}
	if err := xml.Unmarshal(content, &m); err != nil {
		return "", err
	}

	for _, p := range m.Projects {
		if p.Name == project {
			return p.Revision, nil
		}
	}

	return "", fmt.Errorf("project %q is not found in the jiri manifest", project)
}

func getGIStatus(ci *changeInfo) (giStatus, error) {
	var name string

	switch ci.Project {
	case "fuchsia":
		name = "stem"
	case "topaz":
		name = "topaz/minimal"
	case "experiences":
		name = "flower"
	default:
		return giStatusUnknown, nil
	}

	manifest, err := downloadGIManifest(name)
	if err != nil {
		return giStatusUnknown, err
	}

	_, err = getGIRevision(manifest, ci.Project)
	if err != nil {
		return giStatusUnknown, err
	}

	// TODO: Finish implementation.
	return giStatusPassed, nil
}
