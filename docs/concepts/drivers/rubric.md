# Drivers rubric

## Overview

This document describes the rules for writing new drivers.

## Location

Many drivers are located under /src/devices in folders named
according to the driver type. For instance
[/src/devices/clock/drivers](/src/devices/clock/drivers),
[/src/devices/usb/drivers](/src/devices/usb/drivers), etc. Some
functional areas include their drivers within their own directories,
for instance [/src/media/audio/drivers](/src/media/audio/drivers),
[/src/graphics/drivers](/src/graphics/drivers), etc. New drivers must
be located alongside other drivers of the same type. If there is no
existing folder were a new driver should be logically placed under,
then a new folder needs to be added to [/src/devices](/src/devices),
named appropriately and include a drivers folder under it.

## OWNERS

As with any other code in Fuchsia an OWNER must approve the addition
of a new driver. The OWNERS file to check for approval depends on the
location where the driver is added.
