// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

/*
#include "dnssdfinder.h"
#include <errno.h>
#include <string.h>
*/
import "C"

import (
	"context"
	"fmt"
	"net"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"unsafe"
)

const (
	fuchsiaMDNSServiceMac = "_fuchsia._udp."
	daemonPollTimeoutMs   = 10
)

type dnsSDFinder struct {
	// From finders.go
	deviceFinderBase

	deviceChannel chan *fuchsiaDevice
	wg            sync.WaitGroup
}

type dnsSDRef struct {
	cptr *C.DNSServiceRef
}

type dnsRefAllocator func(cptr unsafe.Pointer) dnsSDError

func defaultDNSRefAlloc(cptr unsafe.Pointer) dnsSDError {
	return dnsSDError(C.dnsAllocate((*C.DNSServiceRef)(cptr)))
}

func newDNSSDRef(allocator dnsRefAllocator) (*dnsSDRef, dnsSDError) {
	if allocator == nil {
		allocator = defaultDNSRefAlloc
	}
	var cptr C.DNSServiceRef
	d := &dnsSDRef{}
	d.cptr = &cptr
	if err := allocator(unsafe.Pointer(d.cptr)); err != dnsSDNoError {
		return nil, err
	}
	runtime.SetFinalizer(d, freeDNSSDRef)
	return d, 0
}

func freeDNSSDRef(d *dnsSDRef) {
	cptr := atomic.SwapPointer(
		(*unsafe.Pointer)(unsafe.Pointer(d.cptr)), // Converts d.cptr from *C.DNSServiceRef to (void **) so this function works.
		unsafe.Pointer(C.NULL))
	if cptr != unsafe.Pointer(C.NULL) {
		C.dnsDeallocate(C.DNSServiceRef(cptr))
	}
}

type pollingFunction func(*dnsSDContext, int) (bool, syscall.Errno)

func defaultPollingFunction(d *dnsSDContext, timeout int) (bool, syscall.Errno) {
	var errno C.int
	res := C.dnsPollDaemon(*d.ref.cptr, daemonPollTimeoutMs, &errno)
	if res < 0 {
		return false, syscall.Errno(errno)
	}
	if res == 0 {
		return false, 0
	}
	return true, 0
}

type dnsSDContext struct {
	ref         *dnsSDRef
	finder      *dnsSDFinder
	idx         uintptr
	pollingFunc pollingFunction
}

var (
	cm           sync.RWMutex
	clientOffset uintptr
	contexts     = map[uintptr]*dnsSDContext{}
)

// newDNSSDContext Attempts to allocate a new connection with the DNS-SD daemon, returning nil
// and writing an error to the finder's `deviceChannel` if unable to do so, else returns a new
// client context.
func newDNSSDContext(m *dnsSDFinder, allocator dnsRefAllocator) *dnsSDContext {
	cm.Lock()
	defer cm.Unlock()
	dnsSDRef, err := newDNSSDRef(allocator)
	if err != dnsSDNoError {
		go func() {
			m.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("allocating DNS-SD ref returned %w", err),
			}
		}()
		return nil
	}
	ctx := &dnsSDContext{
		ref:    dnsSDRef,
		idx:    clientOffset,
		finder: m,
	}
	contexts[clientOffset] = ctx
	clientOffset++
	return ctx
}

func (d *dnsSDContext) pollWrapper() (bool, syscall.Errno) {
	if d.pollingFunc != nil {
		return d.pollingFunc(d, daemonPollTimeoutMs)
	}
	return defaultPollingFunction(d, daemonPollTimeoutMs)
}

// poll polls the daemon, firing off callbacks if necessary.
func (d *dnsSDContext) poll() {
	var results bool
	var errno syscall.Errno
	for {
		results, errno = d.pollWrapper()
		if errno != 0 {
			// `Timeout()` is a subset of the checks included in
			// `Temporary()` and must be checked first.
			if errno.Timeout() {
				return
			}
			if errno.Temporary() {
				continue
			}
			go func() {
				d.finder.deviceChannel <- &fuchsiaDevice{
					err: fmt.Errorf("polling dns-sd daemon: %w", errno),
				}
			}()
			return
		}

		break
	}

	if results {
		if err := dnsSDError(C.dnsProcessResults(*d.ref.cptr)); err != dnsSDNoError {
			go func() {
				d.finder.deviceChannel <- &fuchsiaDevice{
					err: fmt.Errorf("dns-sd process results returned: %w", err),
				}
			}()
		}
	}
}

// processResults runs an async loop that waits for either the context to expire
// or for results to come in from the dns-sd daemon. When completed, marks it as
// such in the parent work group.
func (d *dnsSDContext) processResults(ctx context.Context) {
	d.finder.wg.Add(1)
	ctx, cancel := context.WithTimeout(ctx, d.finder.cmd.timeout)
	go func() {
		defer cancel()
		defer d.finder.wg.Done()
		defer freeDNSSDRef(d.ref)
		for {
			select {
			case <-ctx.Done():
				return
			default:
				d.poll()
			}
		}
	}()
}

func (m *dnsSDFinder) dnsSDResolve(nodename string) {
	dctx := newDNSSDContext(m, nil)
	if dctx == nil {
		return
	}
	resolveString := fmt.Sprintf("%s.%s", nodename, "local")
	cString := C.CString(resolveString)
	defer C.free(unsafe.Pointer(cString))
	if err := dnsSDError(C.dnsResolve(cString, dctx.ref.cptr, C.bool(m.cmd.ipv4), C.bool(m.cmd.ipv6), unsafe.Pointer(dctx.idx))); err != dnsSDNoError {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("received error from dnsResolve: %w", err),
			}
		}()
		return
	}
	dctx.processResults(context.Background())
}

func (m *dnsSDFinder) dnsSDBrowse(ctx context.Context) {
	dctx := newDNSSDContext(m, nil)
	if dctx == nil {
		return
	}
	cString := C.CString(fuchsiaMDNSServiceMac)
	defer C.free(unsafe.Pointer(cString))
	if err := dnsSDError(C.dnsBrowse(cString, dctx.ref.cptr, unsafe.Pointer(dctx.idx))); err != dnsSDNoError {
		go func() {
			m.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("dnsBrowse: %w", err),
			}
		}()
		return
	}
	dctx.processResults(ctx)
}

func getDNSSDContext(idx uintptr) *dnsSDContext {
	cm.RLock()
	defer cm.RUnlock()
	return contexts[idx]
}

func (m *dnsSDFinder) list(ctx context.Context, f chan *fuchsiaDevice) error {
	m.deviceChannel = f
	m.dnsSDBrowse(ctx)
	return nil
}

func (m *dnsSDFinder) resolve(ctx context.Context, f chan *fuchsiaDevice, nodenames ...string) error {
	m.deviceChannel = f
	for _, nodename := range nodenames {
		m.dnsSDResolve(nodename)
	}
	return nil
}

func (m *dnsSDFinder) close() {
	// Since the mDNS daemon is being polled, the longest this will wait is
	// daemonPollTimeoutMs milliseconds (and however long it will take to
	// complete any potential last-millisecond results processing in the
	// worst case) past the deadline before all worker threads exit.
	m.wg.Wait()
}

func newDNSSDFinder(cmd *devFinderCmd) *dnsSDFinder {
	var wg sync.WaitGroup
	return &dnsSDFinder{deviceFinderBase{cmd: cmd}, nil, wg}
}

func browseCallback(err dnsSDError, nodename string, dctx *dnsSDContext) {
	if err != dnsSDNoError {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("dns-sd browse callback error: %w", err),
			}
		}()
		return
	}
	dctx.finder.dnsSDResolve(nodename)
}

func resolveCallback(err dnsSDError, hostname, ipString string, iface *net.Interface, dctx *dnsSDContext) {
	if err != dnsSDNoError {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("dns-sd browse callback error: %w", err),
			}
		}()
		return
	}
	if net.ParseIP(ipString) == nil {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("unable to parse IP received from dns-sd %s", ipString),
			}
		}()
		return
	}
	go func() {
		fdev := &fuchsiaDevice{
			domain: strings.ReplaceAll(hostname, ".local.", ""),
			addr:   net.ParseIP(ipString),
		}
		if iface != nil && (fdev.addr.IsLinkLocalMulticast() || fdev.addr.IsLinkLocalUnicast()) {
			fdev.zone = iface.Name
		}
		if dctx.finder.cmd.localResolve {
			var err error
			fdev, err = fdev.outbound()
			if err != nil {
				dctx.finder.deviceChannel <- &fuchsiaDevice{err: err}
				return
			}
		}
		dctx.finder.deviceChannel <- fdev
	}()
}

//////// C Callbacks

//export browseCallbackGoFunc
func browseCallbackGoFunc(err C.int, replyName *C.char, idx unsafe.Pointer) {
	browseCallback(dnsSDError(err), C.GoString(replyName), getDNSSDContext(uintptr(idx)))
}

//export resolveCallbackGoFunc
func resolveCallbackGoFunc(err C.int, fullname, ip *C.char, zoneIdx uint32, idx unsafe.Pointer) {
	dctx := getDNSSDContext(uintptr(idx))
	var iface *net.Interface
	if zoneIdx > 0 {
		var err error
		iface, err = net.InterfaceByIndex(int(zoneIdx))
		if err != nil {
			go func() {
				dctx.finder.deviceChannel <- &fuchsiaDevice{
					err: fmt.Errorf("error getting iface: %w", err),
				}
			}()
			return
		}
	}
	resolveCallback(dnsSDError(err), C.GoString(fullname), C.GoString(ip), iface, dctx)
}
