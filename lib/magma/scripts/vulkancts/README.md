1. Ensure cmake is in your path

2. Create /data persistent storage filesystem on Acer internal ssd

NOTE: assumes you have a usb device at /dev/class/block/000

netruncmd magenta 'minfs /dev/class/block/001 mkfs'

then reboot

3. Build and copy binaries

magma/scripts/vulkancts/build.sh && magma/scripts/vulkancts/copy.sh

4. Make sure netstack is running

netstack&

5. Execute

netruncmd : '/data/vulkancts/run.sh'
