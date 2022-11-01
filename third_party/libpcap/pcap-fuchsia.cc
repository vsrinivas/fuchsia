// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fuchsia's libpcap module.
//
// As per doc/README.capture-module,
//
//   The module should be a C source file, with a name of the form
//   pcap-{MOD}.c, where {MOD} is a name appropriate for your device; for
//   example, the support for DAG cards is in pcap-dag.c, and the support for
//   capturing USB traffic on Linux is pcap-usb-linux.c.
//
//   Your module is assumed to support one or more named devices.  The names
//   should be relatively short names, containing only lower-case
//   alphanumeric characters, consisting of a prefix that ends with an
//   alphabetic character and, if there can be more than one device instance,
//   possibly followed by a numerical device ID, such as "mydevice" or
//   "mydevice0"/"mydevice1"/....  If you have more than one type of device
//   that you can support, you can have more than one prefix, each of which
//   can be followed by a numerical device ID.
//
//   The two exported functions that your module must provide are routines to
//   provide a list of device instances and a program to initialize a
//   created-but-not-activated pcap_t for an instance of one of your devices.
//
// See pcap_platform_finddevs and pcap_create_interface for more details.

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <lib/fit/defer.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <iterator>
#include <limits>
#include <mutex>

#include <netpacket/packet.h>

#include "pcap-fuchsia.h"
#include "pcap-int.h"
#include "pcap/sll.h"

namespace {

int loopback_ifindex() {
  static std::once_flag once_flag;
  static int loopback_ifindex;

  std::call_once(once_flag, [&]() {
    constexpr char kLoopbackDeviceName[] = "lo";

    loopback_ifindex = if_nametoindex(kLoopbackDeviceName);
    if (loopback_ifindex < 0) {
      ZX_PANIC("failed to get interface index for loopback (%s): %s", kLoopbackDeviceName,
               strerror(errno));
    }
  });

  return loopback_ifindex;
}

constexpr char kAnyDeviceDescription[] = "A device that captures on all interfaces";
constexpr char kAnyDeviceName[] = "any";
constexpr int kAnyDeviceIndex = std::numeric_limits<int>::lowest();

pcap_fuchsia *fuchsia_handle(pcap_t *handle) { return static_cast<pcap_fuchsia *>(handle->priv); }

void pcap_cleanup_fuchsia(pcap_t *handle) {
  pcap_fuchsia *const handlep = fuchsia_handle(handle);

  if (handlep->poll_breakloop_fd != 0) {
    close(handlep->poll_breakloop_fd);
    handlep->poll_breakloop_fd = 0;
  }

  pcap_cleanup_live_common(handle);
}

void pcap_setnonblock_fuchsia(pcap_t *handle, bool nonblock) {
  pcap_fuchsia *const handlep = fuchsia_handle(handle);

  if (nonblock) {
    handlep->poll_timeout = 0;
  } else {
    handlep->poll_timeout = handle->opt.timeout;
  }
}

// Returns false iff the packet should be dropped.
bool pcap_check_direction_fuchsia(pcap_direction_t direction, const sockaddr_ll *sll) {
  if (sll->sll_pkttype == PACKET_OUTGOING) {
    switch (direction) {
      case PCAP_D_INOUT:
        // If we are interested in both input and output packets and this packet
        // is outgoing on loopback, drop it in favour of the incoming looped-back
        // packet so clients don't see the same packet twice.
        return sll->sll_ifindex != loopback_ifindex();
      case PCAP_D_IN:
        // We are only interested in incoming packets.
        return false;
      case PCAP_D_OUT:
        return true;
      default:
        ZX_PANIC("unhandled direction value = %d", direction);
    }
  }

  if (direction == PCAP_D_OUT) {
    // We are only interested in outgoing packets.
    return false;
  }

  return true;
}

// Activate a device.
//
// As per doc/README.capture-module,
//
//   Your activate routine takes, as an argument, a pointer to the pcap_t
//   being activated, and returns an int.
//
//   The perameters set for the device in the pcap_create() call, and after
//   that call(), are mostly in the opt member of the pcap_t:
//
//   	device
//   		the name of the device
//
//   	timeout
//   		the buffering timeout, in milliseconds
//
//   	buffer_size
//   		the buffer size to use
//
//   	promisc
//   		1 if promiscuous mode is to be used, 0 otherwise
//
//   	rfmon
//   		1 if monitor mode is to be used, 0 otherwise
//
//   	immediate
//   		1 if the device should be in immediate mode, 0 otherwise
//
//   	nonblock
//   		1 if the device should be in non-blocking mode, 0
//   		otherwise
//
//   	tstamp_type
//   		the type of time stamp to supply
//
//   	tstamp_precision
//   		the time stamp precision to supply
//
//   The snapshot member of the pcap_t structure will contain the snapshot
//   length to be used.
//
//   Your routine should attempt to set up the device for capturing.  If it
//   fails, it must return an error indication which is one of the PCAP_ERROR
//   values.  For PCAP_ERROR, it must also set the errbuf member of the
//   pcap_t to an error string.  For PCAP_ERROR_NO_SUCH_DEVICE and
//   PCAP_ERROR_PERM_DENIED, it may set it to an error string providing
//   additional information that may be useful for debugging, or may just
//   leave it as a null string.
//
//   If it succeeds, it must set certain function pointers in the pcap_t
//   structure:
//
//   	read_op
//   		called whenever packets are to be read
//
//   	inject_op
//   		called whenever packets are to be injected
//
//   	setfilter_op
//   		called whenever pcap_setfilter() is called
//
//   	setdirection_op
//   		called whenever pcap_setdirection() is called
//
//   	set_datalink_op
//   		called whnever pcap_set_datalink() is called
//
//   	getnonblock_op
//   		called whenever pcap_getnonblock() is called
//
//   	setnonblock_op
//   		called whenever pcap_setnonblock() is called
//
//   	stats_op
//   		called whenever pcap_stats() is called
//
//   	cleanup_op
//   		called if the activate routine fails or pcap_close() is
//   		called
//
//   and must also set the linktype member to the DLT_ value for the device.
//
//   On UN*Xes, if the device supports waiting for packets to arrive with
//   select()/poll()/epoll()/kqueues etc., it should set the selectable_fd
//   member of the structure to the descriptor you would use with those
//   calls.  If it does not, then, if that's because the device polls for
//   packets rather than receiving interrupts or other signals when packets
//   arrive, it should have a struct timeval in the private data structure,
//   set the value of that struct timeval to the poll timeout, and set the
//   required_select_timeout member of the pcap_t to point to the struct
//   timeval.
int pcap_activate_fuchsia(pcap_t *handle) {
  pcap_fuchsia *const handlep = fuchsia_handle(handle);

  // Cancelled on success.
  auto on_error_defer_cleanup = fit::defer([&] { pcap_cleanup_fuchsia(handle); });

  if (handle->opt.rfmon) {
    return PCAP_ERROR_RFMON_NOTSUP;
  }

  if (strcmp(handle->opt.device, kAnyDeviceName) == 0) {
    handlep->ifindex = kAnyDeviceIndex;
  } else {
    handlep->ifindex = if_nametoindex(handle->opt.device);
    if (handlep->ifindex < 0) {
      if (errno == ENXIO) {
        return PCAP_ERROR_NO_SUCH_DEVICE;
      }
      pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno, "if_nametoindex");
      return PCAP_ERROR;
    }
  }

  if (handle->opt.promisc) {
    if (handlep->ifindex == loopback_ifindex()) {
      fprintf(stderr, "Loopback does not have a promiscuous mode; ignoring...\n");
    } else if (handlep->ifindex == kAnyDeviceIndex) {
      fprintf(stderr, "The any device does not have a promiscuous mode; ignoring...\n");
    } else {
      // TODO(https://fxbug.dev/88038): Put the device in promiscuous mode.
      pcap_strlcpy(handle->errbuf, "promiscuous mode not supported", PCAP_ERRBUF_SIZE);
      return PCAP_WARNING_PROMISC_NOTSUP;
    }
  }

  // Open a packet socket.
  //
  // TODO(https://fxbug.dev/88035): Only open a SOCK_DGRAM packet socket if this
  // is the any device so that we can parse the link headers of the device we
  // are bound to.
  handle->linktype = DLT_LINUX_SLL2;
  fprintf(stderr, "Only cooked (SOCK_DGRAM) packet sockets supported on Fuchsia\n");
  // Always start the socket in non-blocking mode - we block with poll.
  handle->fd = socket(AF_PACKET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (handle->fd < 0) {
    pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno, "socket");
    if (errno == EPERM || errno == EACCES) {
      // No access to packet sockets.
      return PCAP_ERROR_PERM_DENIED;
    }
    return PCAP_ERROR;
  }
  handle->selectable_fd = handle->fd;

  // Bind the packet socket to the device and start receiving packets.
  const sockaddr_ll sll = {
      .sll_family = AF_PACKET,
      .sll_protocol = htons(ETH_P_ALL),
      .sll_ifindex = (handlep->ifindex == kAnyDeviceIndex) ? 0 : handlep->ifindex,
  };
  if (bind(handle->fd, reinterpret_cast<const sockaddr *>(&sll), sizeof(sll)) != 0) {
    pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno, "bind");

    switch (errno) {
      case ENETDOWN:
        return PCAP_ERROR_IFACE_NOT_UP;
      case ENODEV:
        return PCAP_ERROR_NO_SUCH_DEVICE;
      default:
        return PCAP_ERROR;
    }
  }

  pcap_setnonblock_fuchsia(handle, handle->opt.nonblock != 0);

  // Allocate a buffer to hold a single packet.
  if (handle->snapshot <= 0 || handle->snapshot > MAXIMUM_SNAPLEN) {
    handle->snapshot = MAXIMUM_SNAPLEN;
  }
  handle->buffer = malloc(handle->snapshot);
  if (handle->buffer == NULL) {
    pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno,
                              "can't allocate packet buffer");
    return PCAP_ERROR;
  }
  handle->bufsize = handle->snapshot;

  // Attempts to read packets from the device.
  //
  // As per doc/README.capture-module,
  //
  //   The read_op routine is called when pcap_dispatch(), pcap_loop(),
  //   pcap_next(), or pcap_next_ex() is called.  It is passed the same
  //   arguments as pcap_dispatch() is called.
  //
  //   The routine should first check if the break_loop member of the pcap_t is
  //   non-zero and, if so, set that member to zero and return
  //   PCAP_ERROR_BREAK.
  //
  //   Then, if the pcap_t is in blocking mode (as opposed to non-blocking
  //   mode), and there are no packets immediately available to be passed to
  //   the callback, it should block waiting for packets to arrive, using the
  //   buffering timeout, first, and read packets from the device if necessary.
  //
  //   Then it should loop through the available packets, calling the callback
  //   routine for each packet:
  //
  //   	If the PACKET_COUNT_IS_UNLIMITED() macro evaluates to true when
  //   	passed the packet count argument, the loop should continue until
  //   	there are no more packets immediately available or the
  //   	break_loop member of the pcap_t is non-zero.  If the break_loop
  //   	member is fount to be non-zero, it should set that member to
  //   	zero and return PCAP_ERROR_BREAK.
  //
  //   	If it doesn't evaluat to true, then the loop should also
  //   	terminate if the specified number of packets have been delivered
  //   	to the callback.
  //
  //   Note that there is *NO* requirement that the packet header or data
  //   provided to the callback remain available, or valid, after the callback
  //   routine returns; if the callback needs to save the data for other code
  //   to use, it must make a copy of that data.  This means that the module is
  //   free to, for example, overwrite the buffer into which it read the
  //   packet, or release back to the kernel a packet in a memory-mapped
  //   buffer shared between the kernel and userland, after the callback
  //   returns.
  //
  //   If an error occurs when reading packets from the device, it must set the
  //   errbuf member of the pcap_t to an error string and return PCAP_ERROR.
  //
  //   If no error occurs, it must return the number of packets that were
  //   supplied to the callback routine.
  handle->read_op = [](pcap_t *handle, int max_packets, pcap_handler callback,
                       u_char *user) -> int {
    pcap_fuchsia *const handlep = fuchsia_handle(handle);

    // Wait for a packet to be available.
    for (;;) {
      pollfd pfds[] = {
          // Wait for a packet.
          {
              .fd = handle->fd,
              .events = POLLIN,
          },
          // Wait for a signal to break out of the poll.
          {
              .fd = handlep->poll_breakloop_fd,
              .events = POLLIN,
          },
      };

      int n = poll(pfds, std::size(pfds), handlep->poll_timeout);
      if (n == 0) {
        // We timed-out.
        break;
      }
      const pollfd &socket_fd = pfds[0];
      const pollfd &break_fd = pfds[1];

      if (n < 0) {
        if (errno != EINTR) {
          pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno, "poll error");
          return PCAP_ERROR;
        }

        // poll returned because a signal occured before a requested event.
        continue;
      }

      if (socket_fd.revents == POLLIN) {
        // A packet is avalable.
        break;
      }

      if (socket_fd.revents != 0) {
        if (socket_fd.revents & POLLNVAL) {
          pcap_strlcpy(handle->errbuf, "invalid polling request on packet socket",
                       PCAP_ERRBUF_SIZE);
          return PCAP_ERROR;
        }

        if (socket_fd.revents & (POLLHUP | POLLRDHUP)) {
          pcap_strlcpy(handle->errbuf, "hangup on packet socket", PCAP_ERRBUF_SIZE);
          return PCAP_ERROR;
        }

        if (socket_fd.revents & POLLERR) {
          int err;
          socklen_t errlen = sizeof(err);
          if (getsockopt(handle->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
            err = errno;
          }

          pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, err,
                                    "error on packet socket");
          return PCAP_ERROR;
        }
      }

      // Were we signalled to break the loop?
      if (break_fd.revents & POLLIN) {
        uint64_t value;
        ssize_t nread = read(handlep->poll_breakloop_fd, &value, sizeof(value));
        if (nread < 0) {
          pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno,
                                    "error reading from breakloop FD");
          return PCAP_ERROR;
        }

        if (nread != 0 && (size_t)nread < sizeof(value)) {
          snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
                   "short read from event FD: expected %zu, got %zd", sizeof(value), nread);
          return PCAP_ERROR;
        }

        if (handle->break_loop) {
          handle->break_loop = 0;
          return PCAP_ERROR_BREAK;
        }

        // We were signalled to break the loop but the break_loop flag was not
        // set. We continue to try again.
        continue;
      }
    }

    int processed = 0;
    while (handle->break_loop == 0 &&
           ((processed < max_packets) || PACKET_COUNT_IS_UNLIMITED(max_packets))) {
      sockaddr_ll src_addr;
      socklen_t src_addr_len = sizeof(src_addr);
      uint8_t *buffer = static_cast<uint8_t *>(handle->buffer);
      size_t bufsize = handle->bufsize;

      // reserve the first sizeof(sll2_header) bytes in the packet buffer
      // for the SLL2 header.
      if (bufsize < sizeof(sll2_header)) {
        snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
                 "bufsize shorter than expected; got %zu, want atleast %zu", bufsize,
                 sizeof(sll2_header));
        return PCAP_ERROR;
      }
      ssize_t nread =
          recvfrom(handle->fd, buffer + sizeof(sll2_header), bufsize - sizeof(sll2_header), 0,
                   reinterpret_cast<sockaddr *>(&src_addr), &src_addr_len);
      if (nread < 0) {
        if (errno == EAGAIN) {
          // Nothing else to read for now.
          break;
        }
        pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno,
                                  "error reading packet socket");
        return PCAP_ERROR;
      }
      if (src_addr_len != sizeof(src_addr)) {
        snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "invalid address length = %d, want = %lu",
                 src_addr_len, sizeof(src_addr));
        return PCAP_ERROR;
      }
      if (nread == 0) {
        // No more to read.
        break;
      }

      if (!pcap_check_direction_fuchsia(handle->direction, &src_addr)) {
        // We are not interested in this packet's direction.
        continue;
      }

      pcap_pkthdr pcap_header = {
          .caplen = static_cast<bpf_u_int32>(nread + sizeof(sll2_header)),
          .len = static_cast<bpf_u_int32>(nread + sizeof(sll2_header)),
      };

      if (gettimeofday(&pcap_header.ts, NULL) != 0) {
        pcap_fmt_errmsg_for_errno(handle->errbuf, PCAP_ERRBUF_SIZE, errno, "gettimeofday");
        return PCAP_ERROR;
      }

      sll2_header sll2 = {
          .sll2_protocol = src_addr.sll_protocol,
          .sll2_reserved_mbz = 0,
          .sll2_if_index = htonl(src_addr.sll_ifindex),
          .sll2_hatype = src_addr.sll_hatype,
          .sll2_pkttype = src_addr.sll_pkttype,
          .sll2_halen = src_addr.sll_halen,
      };
      memcpy(buffer, &sll2, offsetof(sll2_header, sll2_addr));
      memcpy(buffer + offsetof(sll2_header, sll2_addr), src_addr.sll_addr,
             sizeof(src_addr.sll_addr));

      if ((handle->fcode.bf_insns == NULL) ||
          pcap_filter(handle->fcode.bf_insns, buffer, pcap_header.len, pcap_header.caplen)) {
        handlep->stat.ps_recv++;
        processed++;

        callback(user, &pcap_header, buffer);
      }
    }

    if (handle->break_loop) {
      handle->break_loop = 0;
      return PCAP_ERROR_BREAK;
    }

    return processed;
  };

  // Attempts to inject a packet to the device.
  //
  // As per doc/README.capture-module,
  //
  //   The inject routine is passed a pointer to the pcap_t, a buffer
  //   containing the contents of the packet to inject, and the number of bytes
  //   in the packet.  If the device doesn't support packet injection, the
  //   routine must set the errbuf member of the pcap_t to a message indicating
  //   that packet injection isn't supported and return PCAP_ERROR.  Otherwise,
  //   it should attempt to inject the packet; if the attempt fails, it must
  //   set the errbuf member of the pcap_t to an error message and return
  //   PCAP_ERROR.  Otherwise, it should return the number of bytes injected.
  handle->inject_op = [](pcap_t *handle, const void *buf, int size) -> int {
    pcap_strlcpy(handle->errbuf, "injecting packets isn't supported on Fuchsia", PCAP_ERRBUF_SIZE);
    return PCAP_ERROR;
  };

  // Sets a filter for packets.
  //
  // As per doc/README.capture-module,
  //
  //   The setfilter routine is passed a pointer to the pcap_t and a pointer
  //   to a struct bpf_program containing a BPF program to be used as a filter.
  //   If the mechanism used by your module can perform filtering with a BPF
  //   program, it would attempt to set that filter to the specified program.
  //
  //   If that failed because the program was too large, or used BPF features
  //   not supported by that mechanism, the module should fall back on
  //   filtering in userland by saving a copy of the filter with a call to
  //   install_bpf_program(), setting a flag in the private data instructure
  //   indicating that filtering is being done by the module and, in the read
  //   routine's main loop, checking the flag and, if it's set, calling
  //   pcap_filter(), passing it the fcode.bf_insns member of the pcap_t, the
  //   raw packet data, the on-the-wire length of the packet, and the captured
  //   length of the packet, and only passing the packet to the callback
  //   routine, and counting it, if pcap_filter() returns a non-zero value.
  //   (If the flag is not set, all packets should be passed to the callback
  //   routine and counted, as the filtering is being done by the mechanism
  //   used by the module.)  If install_bpf_program() returns a negative value,
  //   the routine should return PCAP_ERROR.
  //
  //   If the attempt to set the filter failed for any other reason, the
  //   routine must set the errbuf member of the pcap_t to an error message and
  //   return PCAP_ERROR.
  //
  //   If the attempt to set the filter succeeded, or it failed because the
  //   mechanism used by the module rejected it and the call to
  //   install_bpf_program() succeeded, the routine should return 0.
  //
  //   If the mechanism the module uses doesn't support filtering, the pointer
  //   to the setfilter routine can just be set to point to
  //   install_bpf_program; the module does not need a routine of its own to
  //   handle that.
  handle->setfilter_op = install_bpf_program;

  // Sets the direction to filter for.
  //
  // As per doc/README.capture-module,
  //
  //   The setdirection routine is passed a pointer to the pcap_t and a
  //   pcap_direction_t indicating which packet directions should be accepted.
  //   If the module can't arrange to handle only incoming packets or only
  //   outgoing packets, it can set the pointer to the setdirection routine to
  //   NULL, and calls to pcap_setdirection() will fail with an error message
  //   indicating that setting the direction isn't supported.
  handle->setdirection_op = [](pcap_t *handle, pcap_direction_t d) -> int {
    handle->direction = d;
    return 0;
  };

  handle->set_datalink_op = [](pcap_t *handle, int dlt) -> int {
    pcap_strlcpy(handle->errbuf, "setting the datalink isn't support on Fuchsia", PCAP_ERRBUF_SIZE);
    return PCAP_ERROR;
  };

  handle->setnonblock_op = [](pcap_t *handle, int nonblock) -> int {
    pcap_setnonblock_fuchsia(handle, nonblock != 0);
    return 0;
  };

  handle->getnonblock_op = [](pcap_t *handle) -> int {
    pcap_fuchsia *const handlep = fuchsia_handle(handle);
    return handlep->poll_timeout == 0;
  };

  handle->cleanup_op = pcap_cleanup_fuchsia;

  handle->stats_op = [](pcap_t *handle, pcap_stat *stat) -> int {
    pcap_fuchsia *const handlep = fuchsia_handle(handle);
    *stat = handlep->stat;
    return 0;
  };

  handle->breakloop_op = [](pcap_t *handle) {
    pcap_breakloop_common(handle);
    pcap_fuchsia *const handlep = fuchsia_handle(handle);

    uint64_t value = 1;
    ssize_t n = write(handlep->poll_breakloop_fd, &value, sizeof(value));
    if (n < 0) {
      ZX_PANIC("failed to write to breaklook fd; %s", strerror(errno));
    }
    if (n != sizeof(value)) {
      ZX_PANIC("short write to event FD; expected %zu, got %zd", sizeof(value), n);
    }
  };

  // Cancel the deferred cleanup as we did not encounter an error.
  on_error_defer_cleanup.cancel();
  return 0;
}

}  // namespace

// Initializes a created but not yet activated instance of a device.
//
// As per doc/README.capture-module,
//
//   The "initialize the pcap_t" routine takes, as arguments:
//
//   	a pointer to a device name;
//
//   	a pointer to an error message buffer;
//
//   	a pointer to an int.
//
//   It returns a pointer to a pcap_t.
//
//   Your module will probably need, for each pcap_t for an opened device, a
//   private data structure to maintain its own information about the opened
//   device.  These should be allocated per opened instance, not per device;
//   if, for example, mydevice0 can be captured on by more than one program
//   at the same time, there will be more than one pcap_t opened for
//   mydevice0, and so there will be separate private data structures for
//   each pcap_t.  If you need to maintain per-device, rather than per-opened
//   instance information, you will have to maintain that yourself.
//
//   The routine should first check the device to see whether it looks like a
//   device that this module would handle; for example, it should begin with
//   one of the device name prefixes for your module and, if your devices
//   have instance numbers, be followed by a number.  If it is not one of
//   those devices, you must set the integer pointed to by the third
//   argument to 0, to indicate that this is *not* one of the devices for
//   your module, and return NULL.
//
//   If it *is* one of those devices, it should call pcap_create_common,
//   passing to it the error message buffer as the first argument and the
//   size of the per-opened instance data structure as the second argument.
//   If it fails, it will return NULL; you must return NULL in this case.
//
//   If it succeeds, the pcap_t pointed to by the return value has been
//   partially initialized, but you will need to complete the process.  It
//   has a "priv" member, which is a void * that points to the private data
//   structure attached to it; that structure has been initialized to zeroes.
//
//   What you need to set are some function pointers to your routines to
//   handle certain operations:
//
//   	activate_op
//   		the routine called when pcap_activate() is done on the
//   		pcap_t
//
//   	can_set_rfmon_op
//   		the routine called when pcap_can_set_rfmon() is done on
//   		the pcap_t - if your device doesn't support 802.11
//   		monitor mode, you can leave this as initialized by
//   		pcap_create_common(), as that routine will return "no,
//   		monitor mode isn't supported".
//
//   Once you've set the activate_op and, if necessary, the can_set_rfmon_op,
//   you must return the pcap_t * that was returned to you.
pcap_t *pcap_create_interface(const char *device _U_, char *ebuf) {
  pcap_t *const handle = pcap_create_common_fuchsia(ebuf);
  if (handle == NULL) {
    return NULL;
  }

  handle->activate_op = pcap_activate_fuchsia;

  pcap_fuchsia *const handlep = fuchsia_handle(handle);
  handlep->poll_breakloop_fd = eventfd(0, EFD_NONBLOCK);

  return handle;
}

// Returns a list of device instances
//
// As per doc/README.capture-module,
//
//   The "list of device instances" routine takes, as arguments:
//
//   	a pointer to a pcap_if_list_t;
//
//   	a pointer to an error message buffer.
//
//   The error message buffer may be assumed to be PCAP_ERRBUF_SIZE bytes
//   large, but must not be assumed to be larger.  By convention, the routine
//   typically has a name containing "findalldevs".
//
//   The routine should attempt to determine what device instances are
//   available and add them to the list pointed to by the first argument;
//   this may be impossible for some modules, but, for those modules, it may
//   be difficult to capture on the devices using Wirehshark (although it
//   should be possible to capture on them using tcpdump, TShark, or other
//   programs that take a device name on the command line), so we recommend
//   that your routine provide the list of devices if possible.  If it
//   cannot, it should just immediately return 0.
//
//   The routine should add devices to the list by calling the add_dev()
//   routine in libpcap, declared in the pcap-int.h header.  It takes, as
//   arguments:
//
//   	the pointer to the pcap_if_list_t passed as an argument to the
//   	routine;
//
//   	the device name, as described above;
//
//   	a 32-bit word of flags, as provided by pcap_findalldevs();
//
//   	a text description of the device, or NULL if there is no
//   	description;
//
//   	the error message buffer pointer provided to the routine.
//
//   add_dev() will, if it succeeds, return a pointer to a pcap_if_t that was
//   added to the list of devices.  If it fails, it will return NULL; in this
//   case, the error message buffer has been filled in with an error string,
//   and your routine must return -1 to indicate the error.
//
//   If your routine succeeds, it must return 0.  If it fails, it must fill
//   in the error message buffer with an error string and return -1.
int pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf) {
  // Get the list of regular devices.
  int res = pcap_findalldevs_interfaces(
      devlistp, errbuf, [](const char *name) -> int { return 1; } /* can_be_bound */,
      [](const char *name, bpf_u_int32 *flags,
         char *errbuf) { /* get_if_flags */
                         if (*flags & PCAP_IF_LOOPBACK) {
                           *flags |= PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
                         }

                         // TODO(https://fxbug.dev/88036): Set PCAP_IF_WIRELESS
                         // and/or PCAP_IF_CONNECTION_STATUS_* if needed.

                         return 0;
      });
  if (res != 0) {
    return -1;
  }

  // Add the any device used to capture on all devices.
  if (add_dev(devlistp, kAnyDeviceName,
              PCAP_IF_UP | PCAP_IF_RUNNING | PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE,
              kAnyDeviceDescription, errbuf) == NULL) {
    return -1;
  }

  return 0;
}
