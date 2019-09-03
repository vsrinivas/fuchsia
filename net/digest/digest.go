// Copyright 2013 M-Lab
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package digest

import (
	"crypto/md5"
	"crypto/rand"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
)

var (
	ErrNilTransport      = errors.New("transport is nil")
	ErrBadChallenge      = errors.New("challenge is bad")
	ErrAlgNotImplemented = errors.New("algorithm not implemented")
)

// Transport is an implementation of http.RoundTripper that supports HTTP
// digest authentication.
type Transport struct {
	Username  string
	Password  string
	Transport http.RoundTripper
}

// NewTransport creates a new digest transport using http.DefaultTransport.
func NewTransport(username, password string) *Transport {
	return &Transport{
		Username:  username,
		Password:  password,
		Transport: http.DefaultTransport,
	}
}

// RoundTrip makes a request expecting a 401 response that will require digest
// authentication. It creates the credentials it needs and makes a follow-up
// request.
func (t *Transport) RoundTrip(r *http.Request) (*http.Response, error) {
	if t.Transport == nil {
		return nil, ErrNilTransport
	}

	body, err := r.GetBody()
	if err != nil {
		return nil, err
	}
	req, err := http.NewRequest(r.Method, r.URL.String(), body)
	if err != nil {
		return nil, err
	}

	req.Header = make(http.Header)
	for k, s := range r.Header {
		req.Header[k] = s
	}

	// Make a request to get the 401 that contains the challenge.
	res, err := t.Transport.RoundTrip(r)
	if err != nil || res.StatusCode != 401 {
		return res, err
	}
	defer res.Body.Close()

	chal := res.Header.Get("WWW-Authenticate")
	c, err := parseChallenge(chal)
	if err != nil {
		return res, err
	}

	// Generate credentials based on the challenge.
	cr := t.authenticate(req, c)
	auth, err := cr.authorize()
	if err != nil {
		return res, err
	}

	// Make authenticated request.
	req.Header.Set("Authorization", auth)
	return t.Transport.RoundTrip(req)
}

type challenge struct {
	realm     string
	domain    string
	nonce     string
	opaque    string
	stale     string
	algorithm string
	qop       string
}

func parseChallenge(input string) (*challenge, error) {
	const ws = " \n\r\t"
	const qs = `"`
	s := strings.Trim(input, ws)
	if !strings.HasPrefix(s, "Digest ") {
		return nil, ErrBadChallenge
	}
	s = strings.Trim(s[7:], ws)
	sl := strings.Split(s, ",")
	c := &challenge{
		algorithm: "MD5",
	}
	var r []string
	for i := range sl {
		r = strings.SplitN(strings.Trim(sl[i], ws), "=", 2)
		switch r[0] {
		case "realm":
			c.realm = strings.Trim(r[1], qs)
		case "domain":
			c.domain = strings.Trim(r[1], qs)
		case "nonce":
			c.nonce = strings.Trim(r[1], qs)
		case "opaque":
			c.opaque = strings.Trim(r[1], qs)
		case "stale":
			c.stale = strings.Trim(r[1], qs)
		case "algorithm":
			c.algorithm = strings.Trim(r[1], qs)
		case "qop":
			c.qop = strings.Trim(r[1], qs)
		default:
			return nil, ErrBadChallenge
		}
	}
	return c, nil
}

type credentials struct {
	userhash  bool
	username  string
	realm     string
	nonce     string
	uri       string
	algorithm string
	cnonce    string
	opaque    string
	qop       string
	nc        int
	method    string
	password  string
}

func h(data string) string {
	return fmt.Sprintf("%x", md5.Sum([]byte(data)))
}

func (c *credentials) ha1() string {
	return h(fmt.Sprintf("%s:%s:%s", c.username, c.realm, c.password))
}

func (c *credentials) ha2() string {
	return h(fmt.Sprintf("%s:%s", c.method, c.uri))
}

func (c *credentials) response(cnonce string) (string, error) {
	c.nc++
	if c.qop == "auth" {
		if cnonce != "" {
			c.cnonce = cnonce
		} else {
			b := make([]byte, 8)
			io.ReadFull(rand.Reader, b)
			c.cnonce = fmt.Sprintf("%x", b)[:16]
		}
		return h(fmt.Sprintf("%s:%s:%08x:%s:%s:%s",
			c.ha1(), c.nonce, c.nc, c.cnonce, c.qop, c.ha2())), nil
	} else if c.qop == "" {
		return h(fmt.Sprintf("%s:%s:%s", c.ha1(), c.nonce, c.ha2())), nil
	}
	return "", ErrAlgNotImplemented
}

func (c *credentials) authorize() (string, error) {
	if c.algorithm != "MD5" {
		return "", ErrAlgNotImplemented
	}
	if c.qop != "auth" && c.qop != "" {
		return "", ErrAlgNotImplemented
	}
	response, err := c.response("")
	if err != nil {
		return "", err
	}
	sl := []string{}
	sl = append(sl, fmt.Sprintf(`username="%s"`, c.username))
	sl = append(sl, fmt.Sprintf(`realm="%s"`, c.realm))
	sl = append(sl, fmt.Sprintf(`nonce="%s"`, c.nonce))
	sl = append(sl, fmt.Sprintf(`uri="%s"`, c.uri))
	sl = append(sl, fmt.Sprintf(`response="%s"`, response))
	sl = append(sl, fmt.Sprintf(`algorithm="%s"`, c.algorithm))
	sl = append(sl, fmt.Sprintf(`cnonce="%s"`, c.cnonce))
	if c.opaque != "" {
		sl = append(sl, fmt.Sprintf(`opaque="%s"`, c.opaque))
	}
	if c.qop != "" {
		sl = append(sl, fmt.Sprintf(`qop=%s`, c.qop))
	}
	sl = append(sl, fmt.Sprintf("nc=%08x", c.nc))
	if c.userhash {
		sl = append(sl, `userhash="true"`)
	}
	return fmt.Sprintf("Digest %s", strings.Join(sl, ", ")), nil
}

func (t *Transport) authenticate(req *http.Request, c *challenge) *credentials {
	return &credentials{
		username:  t.Username,
		realm:     c.realm,
		nonce:     c.nonce,
		uri:       req.URL.RequestURI(),
		algorithm: c.algorithm,
		opaque:    c.opaque,
		qop:       c.qop,
		nc:        0,
		method:    req.Method,
		password:  t.Password,
	}
}
