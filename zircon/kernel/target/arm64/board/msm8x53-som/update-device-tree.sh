dtc -I dts -o device-tree.dtb -O dtb device-tree.dts
dtc -I dts -o device-tree-2.dtb -O dtb device-tree-2.dts
dtc -I dts -o device-tree-3.dtb -O dtb device-tree-3.dts
cat device-tree-2.dtb device-tree-3.dtb >> device-tree.dtb
