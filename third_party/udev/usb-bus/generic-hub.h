/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __USB_HUB_H
#define __USB_HUB_H

#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb-hub.h>

typedef struct generic_hub {
    int num_ports;
    /* port numbers are always 1 based,
	   so we waste one int for convenience */
    mx_device_t** ports; /* allocated to sizeof(*ports)*(num_ports+1) */

    mx_device_t* hubdev;
    mx_device_t* busdev;
    int hub_address;

    usb_hub_protocol_t* hub_protocol;
    usb_bus_protocol_t* bus_protocol;
} generic_hub_t;

void generic_hub_destroy(generic_hub_t* hub);
int generic_hub_wait_for_port(mx_device_t* device, const int port,
                              const int wait_for,
                              int (*const port_op)(mx_device_t*, int),
                              int timeout_steps, const int step_us);

int generic_hub_scanport(generic_hub_t* const hub, int port);

int generic_hub_init(generic_hub_t* hub, mx_device_t* hubdev, usb_hub_protocol_t* hub_protocol,
                     mx_device_t* busdev, int hub_address);

int generic_hub_attach_dev(generic_hub_t* const hub, const int port);
int generic_hub_detach_dev(generic_hub_t* const hub, const int port);

#endif
