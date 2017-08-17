### Micro-benchmarks

The benchmarks recorded below are obtained by running magenta-benchmarks in a
release build of fuchsia via ssh. When the benchmarks are recorded the Fuchsia user
shell (GPU-accelerated) is running but no user has yet logged in.

These are the running processes at the time of the benchmark:

```
 ps
TASK                    PSS PRIVATE  SHARED NAME
j:1029               796.8M  783.9M         root
  p:1044             558.8M  558.8M     28k bin/devmgr
  j:1078              48.1M   39.6M         magenta-drivers
    p:1752           180.8k    180k     28k devhost:root
    p:1791          1596.8k   1596k     28k devhost:acpi
    p:1840           592.8k    592k     28k devhost:misc
    p:4684            35.0M   26.7M   16.5M devhost:pci#1:8086:5916
    p:4730          1420.8k   1420k     28k devhost:pci#3:8086:9d2f
    p:4858          8540.8k   8540k     28k devhost:pci#6:8086:9d03
    p:4979           532.8k    532k     28k devhost:pci#14:8086:9d71
    p:5052           546.8k    380k    360k devhost:pci#16:8086:15d8
  j:1179            5745.4k   1624k         magenta-services
    p:1182           256.8k    256k     28k crashlogger
    p:1330          4490.8k    440k   8128k virtual-console
    p:1425           266.8k    200k    160k netsvc
    p:4547           176.8k    176k     28k sh:console
    p:6058           184.8k    184k     28k vc:sh
    p:6093           184.8k    184k     28k vc:sh
    p:6148           184.8k    184k     28k vc:sh
  j:1180             184.3M  183.9M         fuchsia
    p:1234           588.8k    588k     28k appmgr
    j:2000           183.8M  183.3M         root
      p:2045         688.8k    688k     28k bootstrap
      j:2336         183.1M  182.6M         boot
        p:2427      1320.8k   1320k     28k wlanstack
        p:2467       316.8k    316k     28k device_runner
        p:2505       320.8k    320k     28k listen
        p:2707      3532.8k   3432k    228k netstack
        p:3001       288.8k    288k     28k device_runner_monitor
        p:3101       468.8k    468k     28k netconnector
        p:3412       336.8k    336k     28k trace_manager
        p:3529       356.8k    356k     28k root_presenter
        p:3587       124.2M  124.0M    404k flutter:userpicker_device_shell
        p:3810       288.8k    288k     28k ktrace_provider
        p:3955       332.8k    332k     28k view_manager
        p:4110        49.2M   49.1M    356k scene_manager
        p:4269       456.8k    456k     28k icu_data
        p:4404       456.8k    456k     28k fonts
        p:24555      296.8k    296k     28k oauth_token_manager
        j:3240      1102.3k   1100k         tcp:22
          j:24964   1102.3k   1100k         fe80::a2b3:ccff:fefb:4467:43218
            p:24965  712.8k    712k     28k /system/bin/sshd
            p:25157  216.8k    216k     28k /boot/bin/sh
            p:25311  172.8k    172k     28k /boot/bin/ps

```

The typical thread load of the system before running the benchmarks:

```
 cpu    load sched (cs ylds pmpts)  pf  sysc ints (hw  tmr tmr_cb) ipi (rs  gen)
   0   0.01%        32    0     0    2    51        0    3      3        9    0
   1   0.03%       255    0     0    3   496        0  115    115       10    0
   2   0.45%        55    0     0    1  4218        6   11     11        8    0
   3   0.02%        24    0     0    0    44        0    7      7        5    0
 cpu    load sched (cs ylds pmpts)  pf  sysc ints (hw  tmr tmr_cb) ipi (rs  gen)
   0   0.00%        17    0     0    1    27        0    1      1        9    0
   1   0.00%        13    0     0    1    19        0    2      2        8    0
   2   0.48%       297    0     0    1  3800        5  129    129        6    1
   3   0.02%        28    0     0    3    45        0    5      5        9    1
 cpu    load sched (cs ylds pmpts)  pf  sysc ints (hw  tmr tmr_cb) ipi (rs  gen)
   0   0.16%       236    0     0   16   483       11   62     62       36   25
   1   0.19%        96    0     0   35   344        0    5      5       27   39
   2   0.57%       161    0     0   15  4715        6   15     15       53   31
   3   0.15%       196    0     0   20   492        0   60     60       32   28
```

It is believed that the running processes has a very minor impact on benchmark results.


## Run 8-17-2017

Intel NUC  Model: NUC7i3BNK

Processor: i3-7100U @ 2.40 GHz (Cache: 3M)
Memory type: DDR4-2133 1.2V SO-DIMM
Max Memory Bandwidth 34.1 GB/s

```
buildid:  GIT_5E66D79D5A167878ACF9A944AF92D0EBB6A60DF2
ELF build ID: d1af6f49136a548ddc216a079f29341e7f4f8df9

Benchmark                               Time           CPU Iterations
---------------------------------------------------------------------
Channel/Create                        896 ns        897 ns     778195
Channel/Write/64                      728 ns        730 ns     950200   83.6564MB/s
Channel/Write/1024                    771 ns        773 ns     906612   1.23397GB/s
Channel/Write/32k                    2147 ns       2149 ns     323812   14.1992GB/s
Channel/Write/64k                    3600 ns       3599 ns     192480   16.9572GB/s
Channel/Read/64                       717 ns        718 ns     972365   85.0027MB/s
Channel/Read/1024                     750 ns        751 ns     934482   1.27003GB/s
Channel/Read/32k                     2102 ns       2101 ns     332341   14.5272GB/s
Channel/Read/64k                     3550 ns       3545 ns     198392    17.217GB/s
ChannelMultiProcess/Write/64        88319 ns       1114 ns     100000   54.7862MB/s
ChannelMultiProcess/Write/1024     238838 ns       1779 ns     100000   548.933MB/s
ChannelMultiProcess/Write/32k      322097 ns      22632 ns      38626   1.34843GB/s
ChannelMultiProcess/Write/64k      207986 ns      39543 ns      19765   1.54353GB/s
ChannelMultiProcess/Read/64          1141 ns       1025 ns     671510    59.561MB/s
ChannelMultiProcess/Read/1024        1292 ns       1148 ns     602280   850.681MB/s
ChannelMultiProcess/Read/32k        19830 ns       5456 ns     128700   5.59307GB/s
ChannelMultiProcess/Read/64k        38534 ns      10650 ns      67404   5.73121GB/s
Event/Create                          591 ns        594 ns    1181620
Event/Close                           681 ns        680 ns    1032407
Event/Signal                          201 ns        199 ns    3506137
EventPair/Create                      870 ns        871 ns     802191
Fifo/Create                          1030 ns       1028 ns     685065
Port/Create/0                         607 ns        610 ns    1146240
Port/Create/0                         607 ns        609 ns    1147258
Socket/Write/64                       698 ns        701 ns    1001960   87.0535MB/s
Socket/Write/1024                     717 ns        720 ns     969184    1.3249GB/s
Socket/Write/32k                     3055 ns       3047 ns     230028   10.0172GB/s
Socket/Write/64k                     5372 ns       5327 ns     131993    11.458GB/s
Socket/Read/64                        649 ns        652 ns    1073736    93.671MB/s
Socket/Read/1024                      673 ns        674 ns    1039222   1.41413GB/s
Socket/Read/32k                      2933 ns       2919 ns     240752   10.4564GB/s
Socket/Read/64k                      5986 ns       5945 ns     122719   10.2659GB/s
Syscall/Null                           69 ns         68 ns   10327057
Syscall/ManyArgs                       77 ns         76 ns    9134297
Thread/Create                        4992 ns       4967 ns     141135
```
