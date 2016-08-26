// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <dev/driver.h>
#include <assert.h>
#include <magenta/compiler.h>
#include <err.h>
#include <trace.h>

extern struct device __start_devices[] __WEAK;
extern struct device __stop_devices[] __WEAK;

status_t device_init_all(void)
{
    status_t res = NO_ERROR;

    struct device *dev = __start_devices;
    while (dev != __stop_devices) {
        status_t code = device_init(dev);

        if (code < 0) {
            TRACEF("Driver init failed for driver \"%s\", device \"%s\", reason %d\n",
                   dev->driver->type, dev->name, code);

            res = code;
        }

        dev++;
    }

    return res;
}

status_t device_fini_all(void)
{
    status_t res = NO_ERROR;

    struct device *dev = __start_devices;
    while (dev != __stop_devices) {
        status_t code = device_fini(dev);

        if (code < 0) {
            TRACEF("Driver fini failed for driver \"%s\", device \"%s\", reason %d\n",
                   dev->driver->type, dev->name, code);

            res = code;
        }

        dev++;
    }

    return res;
}

status_t device_init(struct device *dev)
{
    if (!dev)
        return ERR_INVALID_ARGS;

    DEBUG_ASSERT(dev->driver);

    const struct driver_ops *ops = dev->driver->ops;

    if (ops && ops->init)
        return ops->init(dev);
    else
        return ERR_NOT_SUPPORTED;
}

status_t device_fini(struct device *dev)
{
    if (!dev)
        return ERR_INVALID_ARGS;

    DEBUG_ASSERT(dev->driver);

    const struct driver_ops *ops = dev->driver->ops;

    if (ops && ops->fini)
        return ops->fini(dev);
    else
        return ERR_NOT_SUPPORTED;
}

status_t device_suspend(struct device *dev)
{
    if (!dev)
        return ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(dev->driver);

    const struct driver_ops *ops = dev->driver->ops;

    if (ops && ops->suspend)
        return ops->suspend(dev);
    else
        return ERR_NOT_SUPPORTED;
}

status_t device_resume(struct device *dev)
{
    if (!dev)
        return ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(dev->driver);

    const struct driver_ops *ops = dev->driver->ops;

    if (ops && ops->resume)
        return ops->resume(dev);
    else
        return ERR_NOT_SUPPORTED;
}

