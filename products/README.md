# Fuchsia product definitions

This directory contains definitions for a variety of product configurations. The
products are organized into tracks that build on one another to produce a fully
featured system.

## Baseline track

### Bringup

A minimal system used to exercise kernel and core drivers and to bring up new
boards.

### Core

Self-updating system with core system services, connectivity, and metrics
reporting.

Builds on [bringup](#bringup)

## Workstation track

### Terminal

A system with a simple graphical user interface with a command-line terminal.

Builds on [core](#core).

### Workstation

A system that software developers can use to get work done.

Builds on [terminal](#terminal).

## SDK Image

Base system image to be used along the SDK with core system services,
developer tools and a kitchen sink of available packages.

Builds on [terminal](#terminal).

## Speaker

A modular, ephemeral system for devices that have audio input and output. Lacks
a graphical user interface.

Builds on [core](#core).

## Router

A system for routing network traffic over wireless networks.

Builds on [core](#core).
