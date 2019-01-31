

<!--
    (C) Copyright 2018 The Fuchsia Authors. All rights reserved.
    Use of this source code is governed by a BSD-style license that can be
    found in the LICENSE file.
-->

# Advanced Topics and Tips

This document is part of the [Driver Development Kit tutorial](ddk-tutorial.md) documentation.

## Taking a long time to initialize

What if your device takes a long time to initialize?
When we discussed the **null_bind()** function above, we indicated that a successful
return told the device manager that the driver is now associated with the device.
We can't spend a lot of time in the bind function; we're basically expected to initialize
our device, publish it, and be done.

But your device might need to perform a lengthy initialization operation, such as:

*   enumerate hardware points
*   load firmware
*   negotiate a protocol

and so on, which might take a long time to do.

You can publish your device as "invisible" using the `DEVICE_ADD_INVISIBLE` flag.
This meets the requirements for the binding function, but nobody is able to use
your device (because nobody knows about it yet, because it's not visible).
Now your device can perform the long operations via a background thread.

When your device is ready to service client requests, call
**device_make_visible()**
which will cause it to appear in the pathname space.

### Power savings

Two callouts, **suspend()** and **resume()**, are available for your device in
order to support power or other resource saving features.

Both take a device context pointer and a flags argument, but the flags argument is
used only in the suspend case.

Flag                                | Meaning
------------------------------------|------------------------------------------------------------
`DEVICE_SUSPEND_FLAG_REBOOT`        | The driver should shut itself down in preparation for a reboot or shutdown of the machine
`DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER` | ?
`DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY`   | ?
`DEVICE_SUSPEND_FLAG_POWEROFF`      | The driver should shut itself down in preparation for power off
`DEVICE_SUSPEND_FLAG_MEXEC`         | @@@ almost nobody uses this except for a graphics controller, what does it do? @@@
`DEVICE_SUSPEND_FLAG_SUSPEND_RAM`   | The driver should arrange so that it can be restarted from RAM

> @@@ Yeah, I'm just guessing on the flags; they're used so little...

For documentation purposes, what should I write?
That they are just hints, or that you *must* do something because of a given flag, or ... ?

## Reference: Support functions

This section lists support functions that are provided for your driver to use.

### Accessor functions

The context block that's passed as the first argument to your driver's protocol functions
is an opaque data structure.
This means that in order to access the data elements, you need to call an accessor function:

Function                | Purpose
------------------------|-------------------------------------------
**device_get_name()**        | Retrieves the name of the device
**device_get_parent()**      | Retrieves the parent device of the device

### Administrative functions

The following functions are used to administer the device:

Function                    | Purpose
----------------------------|-------------------------------------------
**device_add()**                 | Adds a device to a parent
**device_make_visible()**        | Makes a device visible
**device_remove()**              | Removes a device from a parent

### Signalling

The following functions are used to set the state of a device:

Function                | Purpose
------------------------|-------------------------------------------
**device_state_set()**       | sets the given signal(s) on the device
**device_state_clr()**       | clears the given signal(s) on the device

We saw these in the `/dev/misc/demo-fifo` handler above.

@@@ Notes only @@@

This section is great for things like open_at(), talking about buffer management,
threading, best practices, advanced options for device_add(), and so on.
I think it can be somewhere between the man page ("printf is used to print a string
and takes the following parameters") and an application note &mdash; I want to see
examples of how to use the functions, what the arguments mean, what the impact of
various design decisions is, that kind of thing.

