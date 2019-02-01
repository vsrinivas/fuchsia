#!/bin/bash

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

usage() {
  echo "usage: ${0} {input} {output}"
  echo "example: ${0}"
  echo "         fuchsia/garnet/lib/magma/include/virtio/virtio_magma.h"
  echo "         biscotti/include/uapi/linux/virtio_magma.h"
  exit 1
}

if [ "$#" -ne 2 ]; then
  usage
fi

if [ ! -f "${1}" ]; then
  usage
fi

cp ${1} ${2}

sed -i -re 's/GARNET_LIB_MAGMA_INCLUDE_VIRTIO_VIRTIO_MAGMA_H_/_LINUX_VIRTIO_MAGMA_H/' ${2}
sed -i -re '/__BEGIN_CDECLS|__END_CDECLS|#include/d' ${2}
sed -i -re 's/ __PACKED|typedef //' ${2}
sed -i -re 's/}[a-zA-Z0-9_ ]*_t;/};/' ${2}
sed -i -re 's/uint8_t/char/' ${2}
sed -i -re 's/uint32_t/__le32/' ${2}
sed -i -re 's/uint64_t/__le64/' ${2}
sed -i -re 's/int32_t/__le32/' ${2}
sed -i -re 's/([a-zA-Z0-9_]*)_t /struct \1 /' ${2}
sed -i -re 's/#define _LINUX_VIRTIO_MAGMA_H/\0\n\n#include <linux\/virtio_ids.h>\n#include <linux\/virtio_config.h>\n#include <linux\/virtmagma.h>\n/' ${2}
