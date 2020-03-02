// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package link

type State int

const (
	StateUnknown State = iota
	StateStarted
	StateDown
	StateClosed
)

type Controller interface {
	Up() error
	Down() error
	Close() error
	SetOnStateChange(func(State))

	SetPromiscuousMode(bool) error
}

func NewLoopbackController() Controller {
	return &loopbackController{}
}

type loopbackController struct {
	onStateChange func(State)
}

func (c *loopbackController) Up() error {
	if f := c.onStateChange; f != nil {
		f(StateStarted)
	}
	return nil
}
func (c *loopbackController) Down() error {
	if f := c.onStateChange; f != nil {
		f(StateDown)
	}
	return nil
}
func (c *loopbackController) Close() error {
	if f := c.onStateChange; f != nil {
		f(StateClosed)
	}
	return nil
}
func (c *loopbackController) SetOnStateChange(f func(State)) {
	c.onStateChange = f
}
func (c *loopbackController) SetPromiscuousMode(bool) error {
	return nil
}
