// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package whereiscl

import (
	"encoding/base64"
	"encoding/xml"
	"fmt"
	"net/url"

	"go.fuchsia.dev/fuchsia/tools/whereiscl/netutil"
)

const fuchsiaURL = "https://fuchsia.googlesource.com"

// GIStatus represents the status of a CL in Global Integration.
type GIStatus string

const (
	giStatusUnknown GIStatus = "UNKNOWN"
	giStatusPassed           = "PASSED"
	giStatusPending          = "PENDING"
)

func downloadGIManifest(name string) ([]byte, error) {
	u, err := url.Parse(fuchsiaURL)
	if err != nil {
		return nil, err
	}
	u.Path = "/integration/+/HEAD/" + name
	q := u.Query()
	q.Set("format", "TEXT")
	u.RawQuery = q.Encode()

	b, err := netutil.HTTPGet(u.String())
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

type gitLogs struct {
	Log []gitLog `json:"log"`
}

type gitLog struct {
	Commit string `json:"commit"`
}

func isAfterGI(project, clRevision, giRevision string) (bool, error) {
	u, err := url.Parse(fuchsiaURL)
	if err != nil {
		return false, err
	}
	u.Path = fmt.Sprintf("/%s/+log/%s..HEAD", project, giRevision)
	q := u.Query()
	q.Set("format", "JSON")
	u.RawQuery = q.Encode()

	logs := gitLogs{}
	if err := netutil.HTTPGetJSON(u.String(), &logs); err != nil {
		return false, err
	}
	for _, log := range logs.Log {
		if log.Commit == clRevision {
			return true, nil
		}
	}
	return false, nil
}

// GetGIStatus returns whether a given ChangeInfo passed Global Integration.
func GetGIStatus(ci *ChangeInfo) (GIStatus, error) {
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

	rev, err := getGIRevision(manifest, ci.Project)
	if err != nil {
		return giStatusUnknown, err
	}

	after, err := isAfterGI(ci.Project, ci.CurrentRevision, rev)
	if err != nil {
		return giStatusUnknown, err
	}
	if after {
		return giStatusPending, nil
	}
	return giStatusPassed, nil
}
