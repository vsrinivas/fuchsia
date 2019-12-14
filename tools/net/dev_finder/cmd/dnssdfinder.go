// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

/*
#include <stdlib.h>
#include <stdbool.h>
#ifdef __APPLE__
#include <dns_sd.h>
#else
typedef struct _DNSServiceRef_t *DNSServiceRef;
#endif

// defined in dnssdfinder_c.c
extern int dnsBrowse(char *service, DNSServiceRef *browseClient, void *ctx);
extern int dnsResolve(char *name, DNSServiceRef *ref, bool ipv4, bool ipv6, void *ctx);
extern int dnsProcessResults(DNSServiceRef ref);
extern int dnsPollDaemon(DNSServiceRef ref, int timeout_milliseconds);
extern int dnsAllocate(DNSServiceRef *ref);
extern void dnsDeallocate(DNSServiceRef ref);
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
	"time"
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

type dnsRefAllocator func(cptr unsafe.Pointer) int

func defaultDNSRefAlloc(cptr unsafe.Pointer) int {
	return int(C.dnsAllocate((*C.DNSServiceRef)(cptr)))
}

func newDNSSDRef(allocator dnsRefAllocator) (*dnsSDRef, int) {
	if allocator == nil {
		allocator = defaultDNSRefAlloc
	}
	var cptr C.DNSServiceRef
	d := &dnsSDRef{}
	d.cptr = &cptr
	if cerr := allocator(unsafe.Pointer(d.cptr)); cerr != 0 {
		return nil, cerr
	}
	runtime.SetFinalizer(d, freeDNSSDRef)
	return d, 0
}

func freeDNSSDRef(d *dnsSDRef) {
	cptr := atomic.SwapPointer(
    (*unsafe.Pointer)(unsafe.Pointer(d.cptr)),  // Converts d.cptr from *C.DNSServiceRef to (void **) so this function works.
    unsafe.Pointer(C.NULL))
	if cptr != unsafe.Pointer(C.NULL) {
		C.dnsDeallocate(C.DNSServiceRef(cptr))
	}
}

type dnsSDContext struct {
	ref    *dnsSDRef
	finder *dnsSDFinder
	idx    uintptr
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
	dnsSDRef, cerr := newDNSSDRef(allocator)
	if cerr != 0 {
		go func() {
			m.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("allocating DNS-SD ref returned %v", cerr),
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

// poll polls the daemon, firing off callbacks if necessary.
func (d *dnsSDContext) poll() {
	res := C.dnsPollDaemon(*d.ref.cptr, daemonPollTimeoutMs)
	if res > 0 {
		if cerr := C.dnsProcessResults(*d.ref.cptr); cerr != 0 {
			go func() {
				d.finder.deviceChannel <- &fuchsiaDevice{
					err: fmt.Errorf("dns-sd process results returned: %v", cerr),
				}
			}()
		}
	} else if res < 0 {
		go func() {
			d.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("polling dns-sd daemon: %v", res),
			}
		}()
	}
}

// processResults runs an async loop that waits for either the context to expire
// or for results to come in from the dns-sd daemon. When completed, marks it as
// such in the parent work group.
func (d *dnsSDContext) processResults(ctx context.Context) {
	d.finder.wg.Add(1)
	ctx, cancel := context.WithTimeout(ctx, time.Duration(d.finder.cmd.timeout)*time.Millisecond)
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
	if res := C.dnsResolve(cString, dctx.ref.cptr, C.bool(m.cmd.ipv4), C.bool(m.cmd.ipv6), unsafe.Pointer(dctx.idx)); res != 0 {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("received error %d from dnsResolve", res),
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
	if res := C.dnsBrowse(cString, dctx.ref.cptr, unsafe.Pointer(dctx.idx)); res != 0 {
		go func() {
			m.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("dnsBrowse returned %d", res),
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
	m.wg.Wait()
}

func newDNSSDFinder(cmd *devFinderCmd) *dnsSDFinder {
	var wg sync.WaitGroup
	return &dnsSDFinder{deviceFinderBase{cmd: cmd}, nil, wg}
}

func browseCallback(cerr int, nodename string, dctx *dnsSDContext) {
	if cerr != 0 {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("dns-sd browse callback error: %v", cerr),
			}
		}()
		return
	}
	dctx.finder.dnsSDResolve(nodename)
}

func resolveCallback(cerr int, hostname, ipString string, iface *net.Interface, dctx *dnsSDContext) {
	if cerr != 0 {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("dns-sd browse callback error: %v", cerr),
			}
		}()
		return
	}
	if net.ParseIP(ipString) == nil {
		go func() {
			dctx.finder.deviceChannel <- &fuchsiaDevice{
				err: fmt.Errorf("unable to parse IP received from dns-sd %v", ipString),
			}
		}()
		return
	}
	go func() {
		var zone string
		if iface != nil {
			zone = iface.Name
		}
		fdev := &fuchsiaDevice{
			domain: strings.ReplaceAll(hostname, ".local.", ""),
			addr:   net.ParseIP(ipString),
			zone:   zone,
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
func browseCallbackGoFunc(cerr C.int, replyName *C.char, idx unsafe.Pointer) {
	browseCallback(int(cerr), C.GoString(replyName), getDNSSDContext(uintptr(idx)))
}

//export resolveCallbackGoFunc
func resolveCallbackGoFunc(cerr C.int, fullname, ip *C.char, zoneIdx uint32, idx unsafe.Pointer) {
	dctx := getDNSSDContext(uintptr(idx))
	var iface *net.Interface
	if zoneIdx > 0 {
		var err error
		iface, err = net.InterfaceByIndex(int(zoneIdx))
		if err != nil {
			go func() {
				dctx.finder.deviceChannel <- &fuchsiaDevice{
					err: fmt.Errorf("error getting iface: %v", err),
				}
			}()
			return
		}
	}
	resolveCallback(int(cerr), C.GoString(fullname), C.GoString(ip), iface, dctx)
}
