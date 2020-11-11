# Copyright 2020 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

dtc -I dts -o device-tree.dtb -O dtb device-tree.dts
dtc -I dts -o device-tree-2.dtb -O dtb device-tree-2.dts
dtc -I dts -o device-tree-3.dtb -O dtb device-tree-3.dts
cat device-tree-2.dtb device-tree-3.dtb >> device-tree.dtb
