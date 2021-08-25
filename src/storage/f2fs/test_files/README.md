f2fs test file
==============
Decompress gzip files.  
$ gunzip -k src/storage/f2fs/test_files/blk1g.bin..gz  

blk1g.bin
=============
* It has three files with different sizes to test direct data blocks and indirect data blocks.
* Properties: '4 KiB' Block size, active_logs=6, noinline_data
* Directory Tree  
.  
..  
100mb.bin  
10mb.bin  
1m.bin  

File contents in blk1g.bin
==========================
Each file is filled with the same pattern of blocks and
the beginning of each block has special header (40B) as below.
  
++++++++++++++++++++++++++++++++++  
|Header (40B) for the 1st block  |  
|0xf2f5 patterns                 |  
|...                             |  
++++++++++++++++++++++++++++++++++  
|Header (40B) for the 2nd block  |  
|0xf2f5 patterns                 |  
|...                             |  
  
The header (40B) consists of magic number(16B), offset (8B) and CRC, and  
the remaining is filled with the '0xf2f5' pattern.  
  
| offset  | content          |  
0000000: caac 0100 0010 0000 0000 0000 0000 0000  ................  
0000010: 0000 0000 0000 0000 0000 0000 0000 0000  ................  
0000020: 0100 0000 5e61 4f8e f2f5 f2f5 f2f5 f2f5  ....^aO.........  
0000030: f2f5 f2f5 f2f5 f2f5 f2f5 f2f5 f2f5 f2f5  ................  
...  
0001000: caac 0100 0010 0000 0000 0000 0000 0000  ................  
0001010: 0010 0000 0000 0000 0000 0000 19c6 0100  ................  
0001020: 0100 0100 5d95 70b5 f2f5 f2f5 f2f5 f2f5  ....].p.........  
0001030: f2f5 f2f5 f2f5 f2f5 f2f5 f2f5 f2f5 f2f5  ................  
0001040: f2f5 f2f5 f2f5 f2f5 f2f5 f2f5 f2f5 f2f5  ................  

fs-tests and unit tests
======================================
* Step #1: set up a build configuration  
$ fx set core.x64 --with //bundles:tests  
  
* Step #2: run 'fx emu'  
$ fx emu -N --headless -hda src/storage/f2fs/test_files/blk1g.bin  
  
* Step #3: run 'fx test'
$ fx test -o f2fs-fs-tests f2fs-slow-fs-tests f2fs-unittest  

Simple compatibility test
==================================
* Step #1: set up a build configuration  
$ fx set core.x64 --with //bundles:tests  
  
* Step #2: run 'fx emu' with an f2fs image  
$ fx emu -N --headless -hda src/storage/f2fs/test_files/blk1g.bin  
  
* Step #3: copy a file in Fuchsia  
$ mkdir tmp/mnt  
$ mount /dev/class/block/000 /tmp/mnt  
$ cd /tmp/mnt  
$ cp 10mb.bin 10mb_dst.bin  
$ umount /tmp/mnt  
$ dm shutdown  

* Step #4: verify the copied file in Linux  
$ mkdir ~/mnt  
$ mount -o noinline_data src/storage/f2fs/test_files/blk1g.bin ~/mnt  
$ diff ~/mnt/10mb.bin ~/mnt/10mb_dst.bin  
  
Caution
========
The current Fuchsia F2FS does not support INLINE_DATA.  
