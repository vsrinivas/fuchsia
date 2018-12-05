// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package webcardlx

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
)

// Webcardlx encapsultes WEBCARDLX PDU connection settings.
type Webcardlx struct {
	// Client is the client used to perform all requests.
	Client *http.Client

	// Host is the network hostname of the PDU.
	Host string

	// Username is the username by which we can log in to the PDU.
	Username string

	// Password is the password by which we can log in to the PDU.
	Password string

	// token is the authentication token obtained by Login.
	token string
}

// LoadState represents possible outlet states.
type LoadState byte

const (
	Off LoadState = iota
	On
	Cycle
)

var (
	loadStateNameToValue = map[string]LoadState{
		"Off":   Off,
		"On":    On,
		"Cycle": Cycle,
	}
	loadStateValueToName = map[LoadState]string{
		Off:   "Off",
		On:    "On",
		Cycle: "Cycle",
	}
)

func (s LoadState) MarshalJSON() ([]byte, error) {
	name, ok := loadStateValueToName[s]
	if !ok {
		return nil, fmt.Errorf("invalid state: %d", s)
	}
	return json.Marshal(name)
}

func (s *LoadState) UnmarshalJSON(data []byte) error {
	var name string
	if err := json.Unmarshal(data, &s); err != nil {
		return fmt.Errorf("LoadState should be a string, got %s", data)
	}
	v, ok := loadStateNameToValue[name]
	if !ok {
		return fmt.Errorf("invalid LoadState %X", s)
	}
	*s = v
	return nil
}

type data struct {
	Token       string `json:"token"`
	Facilities  string `json:"facilities"`
	Origin      string `json:"origin"`
	Realms      string `json:"realms"`
	Role        string `json:"role"`
	Expiration  int    `json:"expiration"`
	IdleTimeout int    `json:"idleTimeout"`
}

type message struct {
	ID    int             `json:"msgid,omitempty"`
	Reply string          `json:"reply,omitempty"`
	Data  json.RawMessage `json:"data,omitempty"`
	Error struct {
		Name    string `json:"name"`
		Message string `json:"message"`
		Status  int    `json:"status"`
	} `json:"error,omitempty"`
}

// Login performs a login and obtains authorization token used for other calls.
func (c *Webcardlx) Login() error {
	q := make(url.Values)
	q.Set("username", c.Username)
	q.Set("password", c.Password)

	u := url.URL{
		Scheme:   "http",
		Host:     c.Host,
		Path:     "/api/AuthenticationControllers/login",
		RawQuery: q.Encode(),
	}

	l := struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}{
		Username: c.Username,
		Password: c.Password,
	}

	b := new(bytes.Buffer)
	if err := json.NewEncoder(b).Encode(l); err != nil {
		return err
	}

	req, err := http.NewRequest("POST", u.String(), b)
	req.Header.Add("Accept", "application/json")
	req.Header.Set("Content-Type", "application/json")
	if err != nil {
		return err
	}
	resp, err := c.Client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	var msg message
	err = json.NewDecoder(resp.Body).Decode(&msg)

	if resp.StatusCode != http.StatusCreated {
		if err != nil {
			return fmt.Errorf("request failed: %d", resp.StatusCode)
		} else {
			return fmt.Errorf("request failed: %s", msg.Error.Message)
		}
	}

	// This is currently unused but will be in the future.
	var data data
	if err := json.Unmarshal(msg.Data, &data); err != nil {
		return err
	}

	if token, ok := resp.Header["Authorization"]; ok {
		c.token = token[0]
	}

	return nil
}

// Loads changes the outlet state.
func (c *Webcardlx) Loads(outlet int, state LoadState) error {
	u := url.URL{
		Scheme: "http",
		Host:   c.Host,
		Path:   fmt.Sprintf("/api/device/loads/%d", outlet),
	}

	s := struct {
		LoadFireState LoadState `json:"loadFireState"`
	}{
		LoadFireState: state,
	}

	b := new(bytes.Buffer)
	if err := json.NewEncoder(b).Encode(s); err != nil {
		return err
	}

	req, err := http.NewRequest("PUT", u.String(), b)
	if err != nil {
		return err
	}
	req.Header.Add("Accept", "application/json")
	req.Header.Add("Authorization", c.token)
	req.Header.Set("Content-Type", "application/json")
	resp, err := c.Client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	var msg message
	err = json.NewDecoder(resp.Body).Decode(&msg)

	if resp.StatusCode != http.StatusOK {
		if err != nil {
			return fmt.Errorf("request failed: %d", resp.StatusCode)
		} else {
			return fmt.Errorf("request failed: %s", msg.Error.Message)
		}
	}

	return nil
}

// Logout performs a logout and discards the authorization token.
func (c *Webcardlx) Logout() error {
	u := url.URL{
		Scheme: "http",
		Host:   c.Host,
		Path:   "/api/AuthenticationControllers/logout",
	}

	req, err := http.NewRequest("POST", u.String(), nil)
	req.Header.Add("Accept", "application/json")
	req.Header.Add("Authorization", c.token)
	req.Header.Set("Content-Type", "application/json")
	if err != nil {
		return err
	}
	resp, err := c.Client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	var msg message
	err = json.NewDecoder(resp.Body).Decode(&msg)

	if resp.StatusCode != http.StatusOK {
		if err != nil {
			return fmt.Errorf("request failed: %d", resp.StatusCode)
		} else {
			return fmt.Errorf("request failed: %s", msg.Error.Message)
		}
	}

	c.token = ""

	return nil
}
