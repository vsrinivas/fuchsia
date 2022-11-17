# Transfer Manifest lib

A generic, portable description of a set of data, both locally and remotely.

## Overview

Broadly, the entries consist of a remote URL, paired with a local file path.
This allows for both an upload tool and download tool to use the same manifest.

## History

The initial Transfer Manifest design is built for use with Product Bundles, but
the long term plan is to use this format for any contract for remotely stored
Fuchsia data (e.g. drivers, code samples, SDK pieces, etc.).
