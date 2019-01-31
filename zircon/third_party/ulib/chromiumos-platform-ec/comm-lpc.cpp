/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <chromiumos-platform-ec/ec_commands.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>
#include <sys/param.h>
#include <unistd.h>
#include <zircon/errors.h>

#define INITIAL_UDELAY 5     /* 5 us */
#define MAXIMUM_UDELAY 10000 /* 10 ms */

/*
 * Wait for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout.
 */
static int wait_for_ec(uint16_t status_addr, int timeout_usec)
{
	int i;
	int delay = INITIAL_UDELAY;

	for (i = 0; i < timeout_usec; i += delay) {
		/*
		 * Delay first, in case we just sent out a command but the EC
		 * hasn't raised the busy flag.  However, I think this doesn't
		 * happen since the LPC commands are executed in order and the
		 * busy flag is set by hardware.  Minor issue in any case,
		 * since the initial delay is very short.
		 */
		usleep(MIN(delay, timeout_usec - i));

		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/* Increase the delay interval after a few rapid checks */
		if (i > 20)
			delay = MIN(delay * 2, MAXIMUM_UDELAY);
	}
	return -1;  /* Timeout */
}

namespace CrOsEc {

zx_status_t CommandLpc3(uint16_t command, uint8_t version,
					   const void *outdata, size_t outsize,
					   void *indata, size_t insize, size_t *actual)
{
	struct ec_host_request rq;
	struct ec_host_response rs;
	const uint8_t *d;
	uint8_t *dout;
	int csum = 0;
	size_t i;

	static_assert(EC_LPC_HOST_PACKET_SIZE <= UINT16_MAX, "");
	/* Fail if output size is too big */
	if (outsize + sizeof(rq) > EC_LPC_HOST_PACKET_SIZE)
		return ZX_ERR_INVALID_ARGS;

	/* Fill in request packet */
	/* TODO(crosbug.com/p/23825): This should be common to all protocols */
	rq.struct_version = EC_HOST_REQUEST_VERSION;
	rq.checksum = 0;
	rq.command = command;
	rq.command_version = version;
	rq.reserved = 0;
	rq.data_len = static_cast<uint16_t>(outsize);

	/* Copy data and start checksum */
	for (i = 0, d = (const uint8_t *)outdata; i < outsize; i++, d++) {
		const uint16_t addr = static_cast<uint16_t>(EC_LPC_ADDR_HOST_PACKET + sizeof(rq) + i);
		outb(*d, addr);
		csum += *d;
	}

	/* Finish checksum */
	for (i = 0, d = (const uint8_t *)&rq; i < sizeof(rq); i++, d++)
		csum += *d;

	/* Write checksum field so the entire packet sums to 0 */
	rq.checksum = (uint8_t)(-csum);

	/* Copy header */
	for (i = 0, d = (const uint8_t *)&rq; i < sizeof(rq); i++, d++) {
		const uint16_t addr = static_cast<uint16_t>(EC_LPC_ADDR_HOST_PACKET + i);
		outb(*d, addr);
	}

	/* Start the command */
	outb(EC_COMMAND_PROTOCOL_3, EC_LPC_ADDR_HOST_CMD);

	if (wait_for_ec(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		return ZX_ERR_TIMED_OUT;
	}

	/* Check result */
	i = inb(EC_LPC_ADDR_HOST_DATA);
	if (i) {
		return ZX_ERR_IO;
	}

	/* Read back response header and start checksum */
	csum = 0;
	for (i = 0, dout = (uint8_t *)&rs; i < sizeof(rs); i++, dout++) {
		const uint16_t addr = static_cast<uint16_t>(EC_LPC_ADDR_HOST_PACKET + i);
		*dout = inb(addr);
		csum += *dout;
	}

	if (rs.struct_version != EC_HOST_RESPONSE_VERSION) {
		return ZX_ERR_IO;
	}

	if (rs.reserved) {
		return ZX_ERR_IO;
	}

	if (rs.data_len > insize) {
		return ZX_ERR_BUFFER_TOO_SMALL;
	}

	/* Read back data and update checksum */
	for (i = 0, dout = (uint8_t *)indata; i < rs.data_len; i++, dout++) {
		const uint16_t addr = static_cast<uint16_t>(EC_LPC_ADDR_HOST_PACKET + sizeof(rs) + i);
		*dout = inb(addr);
		csum += *dout;
	}

	/* Verify checksum */
	if ((uint8_t)csum) {
		return ZX_ERR_IO_DATA_INTEGRITY;
	}

	/* Return actual amount of data received */
	*actual = rs.data_len;
	return ZX_OK;
}

bool IsLpc3Supported()
{
	int i;
	int byte = 0xff;

	/*
	 * Test if the I/O port has been configured for Chromium EC LPC
	 * interface.  Chromium EC guarantees that at least one status bit will
	 * be 0, so if the command and data bytes are both 0xff, very likely
	 * that Chromium EC is not present.  See crosbug.com/p/10963.
	 */
	byte &= inb(EC_LPC_ADDR_HOST_CMD);
	byte &= inb(EC_LPC_ADDR_HOST_DATA);
	if (byte == 0xff) {
		return false;
	}

	/*
	 * Test if LPC command args are supported.
	 *
	 * The cheapest way to do this is by looking for the memory-mapped
	 * flag.  This is faster than sending a new-style 'hello' command and
	 * seeing whether the EC sets the EC_HOST_ARGS_FLAG_FROM_HOST flag
	 * in args when it responds.
	 */
	if (inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID) != 'E' ||
	    inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID + 1) != 'C') {
		return false;
	}

	/* Check which command version we'll use */
	i = inb(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_HOST_CMD_FLAGS);
	return !!(i & EC_HOST_CMD_FLAG_VERSION_3);
}

} // namespace CrOsEc
