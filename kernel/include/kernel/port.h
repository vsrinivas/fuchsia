// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Carlos Pizano-Uribe  cpu@chromium.org
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_PORT_H
#define __KERNEL_PORT_H

#include <sys/types.h>
#include <compiler.h>


__BEGIN_CDECLS;

/* Ports are named, opaque objects and come in three flavors, the
 * write-side, the read-side and a port group which is a collection
 * of read-side ports.
 */

#define PORT_NAME_LEN 12

typedef void *port_t;

typedef struct {
    union {
        char value[8];
        void* pvalue;
    };
} port_packet_t;

typedef struct {
    void *ctx;
    port_packet_t packet;
} port_result_t;

typedef enum {
    PORT_MODE_BROADCAST   = 0,
    PORT_MODE_UNICAST     = 1,
    PORT_MODE_BIG_BUFFER  = 2,
} port_mode_t;

/* Inits the port subsystem
 */
void port_init(void);

/* Make a named write-side port. Broadcast ports can be opened by any
 * number of read-clients. |name| can be up to PORT_NAME_LEN chars. If
 * the write port exists it is returned even if the |mode| does not match.
 *
 * The combination  PORT_MODE_BROADCAST | PORT_MODE_BIG_BUFFER is not
 * valid.
 * Returns ERR_ALREADY_EXISTS if a write-side port with that name
 * already exists. Returns NO_ERROR upon success.
 */
status_t port_create(const char *name, port_mode_t mode, port_t *port);

/* Make a read-side port. Only non-destroyed existing write ports can
 * be opened with this api. Unicast ports can only be opened once. For
 * broadcast ports, each call if successful returns a new port.
 *
 * Returns ERR_NOT_FOUND if a write-side port with |name| cannot be found.
 *
 * Returns ERR_ALREADY_BOUND if the named port is PORT_MODE_UNICAST and
 * has already a connected read-side port.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_open(const char *name, void *ctx, port_t *port);

/* Creates a read-side port group which behaves just like a regular
 * read-side port. A given port can only be assoicated with one port group.
 *
 * Returns ERR_BAD_HANDLE if at least one of the |ports| is not a read-side
 * port.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_group(port_t *ports, size_t count, port_t *group);

/* Adds a read-side port to an existing port group.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_group_add(port_t group, port_t port);

/* Removes a read-side port to an existing port group.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_group_remove(port_t group, port_t port);

/* Write to a port |count| packets, non-blocking, all or none atomic success
 * for unicast ports. It can return ERR_BAD_STATE for multicast.
 *
 * Returns ERR_CHANNEL_CLOSED if there was a read port that got closed.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_write(port_t port, const port_packet_t *pk, size_t count);

/* Read one packet from the port or port group, blocking. The |result| contains
 * the port that the message was read from. If |timeout| is zero the call
 * does not block and returns ERR_TIMED_OUT if there is nothing to read.
 *
 * Returns ERR_CHANNEL_CLOSED if there is nothing to read and the write-side
 * port has been destroyed.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_read(port_t port, lk_time_t timeout, port_result_t *result);

/* Peek one packet from the port, non-blocking.
 *
 * Returns ERR_CHANNEL_CLOSED if there is nothing to read and the write-side
 * port has been destroyed.
 *
 * Returns ERR_NOT_READY if there is no packet pending.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_peek(port_t port, port_result_t *result);

/* Wait for the arrival of one or more packets.
 *
 * Waits up until |timeout| for a packet to be available for reading or the
 * write-side port to be destroyed.
 *
 * If a packet is available for reading it returns NO_ERROR.
 *
 * Otherwise, if the write-side is closed it returns ERR_CHANNEL_CLOSED.
 *
 * Otherwise it returns ERR_TIMED_OUT.
 */
status_t port_wait(port_t port, lk_time_t timeout);

/* Destroy the write-side port, flush queued packets and release all resources,
 * all calls will now fail on that port. Only a closed port can be destroyed.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_destroy(port_t port);

/* Close the read-side port or the write side port. A closed write side port
 * can be opened and the pending packets read. closing a port group does not
 * close the included ports.
 *
 * Returns NO_ERROR upon success.
 */
status_t port_close(port_t port);

__END_CDECLS;

#endif
