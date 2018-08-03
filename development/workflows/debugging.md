# Debugging

This document is a work-in-progress and provides various suggestions
for debugging Fuchsia programs.

## The ZX debugger

For C/C++ code, try zxdb. See the [zxdb docs](https://fuchsia.googlesource.com/garnet/+/master/docs/debugger.md) for more details.

## Backtraces

### Automagic backtraces

Fuchsia starts a program at boot called "crashlogger" that reports
program crashes and prints a backtrace of the crashing thread.


Example:

```
$ /boot/test/debugger-test segfault
[00029.042] 01093.01133> <== fatal exception: process [6354] thread [6403]
[00029.042] 01093.01133> <== fatal page fault, PC at 0x1001df9
[00029.044] 01093.01133>  CS:                   0 RIP:          0x1001df9 EFL:              0x207 CR2:               0x2a
[00029.044] 01093.01133>  RAX:         0x99999999 RBX:          0x1073e90 RCX:                  0 RDX:               0x2a
[00029.044] 01093.01133>  RSI:          0x1073e90 RDI:                0xa RBP:          0x1073e50 RSP:          0x1073e20
[00029.044] 01093.01133>   R8:          0x1003bba  R9:     0x400000127e00 R10:                  0 R11:          0x1073e90
[00029.044] 01093.01133>  R12:          0x1007030 R13:     0x400000127e40 R14:     0x4000000de5f8 R15:                0xe
[00029.044] 01093.01133>  errc:               0x6
[00029.044] 01093.01133> bottom of user stack:
[00029.045] 01093.01133> 0x01073e20: 99999999 00000000 012bc500 00000000 |..........+.....|
[00029.045] 01093.01133> 0x01073e30: 00000003 00000000 0001a680 00004000 |.............@..|
[00029.045] 01093.01133> 0x01073e40: 012b5008 00000000 00087fda 00004000 |.P+..........@..|
[00029.045] 01093.01133> 0x01073e50: 01073e70 00000000 01001e71 00000000 |p>......q.......|
[00029.045] 01093.01133> 0x01073e60: 01073e90 00000000 01007030 00000000 |.>......0p......|
[00029.045] 01093.01133> 0x01073e70: 01073eb0 00000000 01001eb9 00000000 |.>..............|
[00029.045] 01093.01133> 0x01073e80: 01073ea0 00000000 01001e4c 00000000 |.>......L.......|
[00029.045] 01093.01133> 0x01073e90: 99999999 00004000 0001a4b7 00004000 |.....@.......@..|
[00029.045] 01093.01133> 0x01073ea0: 01073ed0 00000000 01007030 00000000 |.>......0p......|
[00029.045] 01093.01133> 0x01073eb0: 01073ef0 00000000 01001eb9 00000000 |.>..............|
[00029.045] 01093.01133> 0x01073ec0: 01073ee0 00000000 01001e4c 00000000 |.>......L.......|
[00029.045] 01093.01133> 0x01073ed0: 99999999 99999999 00006e68 00000000 |........hn......|
[00029.045] 01093.01133> 0x01073ee0: 01073f10 00000000 01007030 00000000 |.?......0p......|
[00029.045] 01093.01133> 0x01073ef0: 01073f30 00000000 01001eb9 00000000 |0?..............|
[00029.045] 01093.01133> 0x01073f00: 01073f20 00000000 01001e4c 00000000 | ?......L.......|
[00029.045] 01093.01133> 0x01073f10: 99999999 99999999 99999999 00000000 |................|
[00029.045] 01093.01133> arch: x86_64
[00029.064] 01093.01133> dso: id=018157dff2cf7052d9a0e198c08875cf080d1fc5 base=0x4000000e1000 name=<vDSO>
[00029.064] 01093.01133> dso: id=bbcefdf213bbac0271e69abc2ea163e9a7bb83a8 base=0x400000000000 name=libc.so
[00029.064] 01093.01133> dso: id=fcb157b4ae828f8456769dcd217f29745902393c base=0x101a000 name=libfdio.so
[00029.064] 01093.01133> dso: id=62e9ae64cd4b9915f4aa0fae8817df47f236f3ef base=0x1011000 name=liblaunchpad.so
[00029.064] 01093.01133> dso: id=2bdcafce47915aa12e0942d59f44726c3832f0a2 base=0x100d000 name=libunittest.so
[00029.064] 01093.01133> dso: id=cc891abcdb9d2f0d8fcae0ae2a98f421a0a592a8 base=0x1008000 name=libtest-utils.so
[00029.064] 01093.01133> dso: id=48fb9f5b9eff4fbd8b23d1f3db32c1904fabc277 base=0x1000000 name=app:/boot/test/debugger-test
[00029.066] 01093.01133> bt#01: pc 0x1001df9 sp 0x1073e20 (app:/boot/test/debugger-test,0x1df9)
[00029.141] 01093.01133> bt#02: pc 0x1001e71 sp 0x1073e60 (app:/boot/test/debugger-test,0x1e71)
[00029.141] 01093.01133> bt#03: pc 0x1001eb9 sp 0x1073e80 (app:/boot/test/debugger-test,0x1eb9)
[00029.142] 01093.01133> bt#04: pc 0x1001e4c sp 0x1073e90 (app:/boot/test/debugger-test,0x1e4c)
[00029.143] 01093.01133> bt#05: pc 0x1001eb9 sp 0x1073ec0 (app:/boot/test/debugger-test,0x1eb9)
[00029.143] 01093.01133> bt#06: pc 0x1001e4c sp 0x1073ed0 (app:/boot/test/debugger-test,0x1e4c)
[00029.144] 01093.01133> bt#07: pc 0x1001eb9 sp 0x1073f00 (app:/boot/test/debugger-test,0x1eb9)
[00029.144] 01093.01133> bt#08: pc 0x1001e4c sp 0x1073f10 (app:/boot/test/debugger-test,0x1e4c)
[00029.145] 01093.01133> bt#09: pc 0x1001eb9 sp 0x1073f40 (app:/boot/test/debugger-test,0x1eb9)
[00029.145] 01093.01133> bt#10: pc 0x1001e4c sp 0x1073f50 (app:/boot/test/debugger-test,0x1e4c)
[00029.146] 01093.01133> bt#11: pc 0x1001ea3 sp 0x1073f90 (app:/boot/test/debugger-test,0x1ea3)
[00029.146] 01093.01133> bt#12: pc 0x100164a sp 0x1073fb0 (app:/boot/test/debugger-test,0x164a)
[00029.147] 01093.01133> bt#13: pc 0x40000001cce6 sp 0x1073ff0 (libc.so,0x1cce6)
[00029.148] 01093.01133> bt#14: pc 0 sp 0x1074000
[00029.149] 01093.01133> bt#15: end
```

Since debug information is currently not available on the target,
a program must be run on the development host to translate the raw
addresses in the backtrace to symbolic form. This program is `symbolize`:

Copy the above to a file, say `backtrace.out`, and then run:

```
bash$ ZIRCON_BUILD_DIR=$FUCHSIA_DIR/out/build-zircon/build-x64
bash$ cat backtrace.out | $FUCHSIA_DIR/zircon/scripts/symbolize \
  --build-dir=$ZIRCON_BUILD_DIR \
  $ZIRCON_BUILD_DIR/system/utest/debugger/debugger.elf
[00029.042] 01093.01133> <== fatal exception: process [6354] thread [6403]
... same output as "raw" backtrace ...
start of symbolized stack:
#01: test_segfault_leaf at /home/fuchsia/zircon/system/utest/debugger/debugger.c:570
#02: test_segfault_doit1 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:585
#03: test_segfault_doit2 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:589
#04: test_segfault_doit1 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:582
#05: test_segfault_doit2 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:589
#06: test_segfault_doit1 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:582
#07: test_segfault_doit2 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:589
#08: test_segfault_doit1 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:582
#09: test_segfault_doit2 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:589
#10: test_segfault_doit1 at /home/fuchsia/zircon/system/utest/debugger/debugger.c:582
#11: test_segfault at /home/fuchsia/zircon/system/utest/debugger/debugger.c:599
#12: main at /home/fuchsia/zircon/system/utest/debugger/debugger.c:640
#13: start_main at /home/fuchsia/zircon/third_party/ulib/musl/src/env/__libc_start_main.c:47
#14: (unknown)
end of symbolized stack
```

For crashes in layers above Zircon run:

```
bash$ cat backtrace.out | fx symbolize
[00030.640] 01103.01117> <== fatal exception: process root_presenter[8202] thread initial-thread[8220]
[00030.640] 01103.01117> <== fatal page fault, PC at 0xa45895d5ca18
[00030.640] 01103.01117>  x0                   0 x1      0xa45895df8778 x2                 0x6 x3                 0x6
[00030.640] 01103.01117>  x4       0xe285ef2e9e8 x5                 0x4 x6           0xe285ef6 x7  0xffffffffffffffff
[00030.640] 01103.01117>  x8       0xe285ef241d8 x9                   0 x10                0xf x11                  0
[00030.640] 01103.01117>  x12                0x1 x13                  0 x14                  0 x15                  0
[00030.640] 01103.01117>  x16     0x74a3a860f300 x17     0x9ec1d6974260 x18                  0 x19     0x7ed835906f20
[00030.640] 01103.01117>  x20      0xe285ef4b210 x21      0xe285ef4b230 x22     0x7ed835906ec0 x23               0x18
[00030.640] 01103.01117>  x24 0xab3f092f31fd126d x25                0x1 x26     0x7ed835907748 x27     0x7ed835907750
[00030.640] 01103.01117>  x28     0x192babebb4b0 x29     0x25638ad21c60 lr      0xa45895d5ca08 sp      0x25638ad21bf0
[00030.640] 01103.01117>  pc      0xa45895d5ca18 psr         0x60000000
[00030.640] 01103.01117>  far               0x28 esr         0x92000004
[00030.640] 01103.01117> bottom of user stack:
[00030.640] 01103.01117> 0x000025638ad21bf0: 35906ed8 00007ed8 00000002 00000000 |.n.5.~..........|
[00030.640] 01103.01117> 0x000025638ad21c00: 35906ef8 00007ed8 359072c8 00007ed8 |.n.5.~...r.5.~..|
[00030.640] 01103.01117> 0x000025638ad21c10: 35908a28 00007ed8 359088d8 00007ed8 |(..5.~.....5.~..|
[00030.640] 01103.01117> 0x000025638ad21c20: 31fd126d ab3f092f 359088e0 00007ed8 |m..1/.?....5.~..|
[00030.640] 01103.01117> 0x000025638ad21c30: 35907758 00007ed8 31fd126d ab3f092f |Xw.5.~..m..1/.?.|
[00030.640] 01103.01117> 0x000025638ad21c40: 35907760 00007ed8 abebb4b0 0000192b |`w.5.~......+...|
[00030.641] 01103.01117> 0x000025638ad21c50: 5ef4b210 00000e28 5ef4b210 00000e28 |...^(......^(...|
[00030.641] 01103.01117> 0x000025638ad21c60: 8ad21ca0 00002563 95d5c564 0000a458 |....c%..d...X...|
[00030.641] 01103.01117> 0x000025638ad21c70: abebb4b0 0000192b 00000005 00000000 |....+...........|
[00030.641] 01103.01117> 0x000025638ad21c80: 5ef4b000 00000e28 3590ae00 00007ed8 |...^(......5.~..|
[00030.641] 01103.01117> 0x000025638ad21c90: 5ef4b210 00000e28 5ef4b000 00000e28 |...^(......^(...|
[00030.641] 01103.01117> 0x000025638ad21ca0: 8ad21d00 00002563 95d5ddec 0000a458 |....c%......X...|
[00030.641] 01103.01117> 0x000025638ad21cb0: 31fd126d ab3f092f 95d5c50c 0000a458 |m..1/.?.....X...|
[00030.641] 01103.01117> 0x000025638ad21cc0: 35908a30 00007ed8 abebb4b0 0000192b |0..5.~......+...|
[00030.641] 01103.01117> 0x000025638ad21cd0: 359088f0 00007ed8 00000005 00000000 |...5.~..........|
[00030.641] 01103.01117> 0x000025638ad21ce0: 5ef4b000 00000e28 3590ae00 00007ed8 |...^(......5.~..|
[00030.641] 01103.01117> arch: aarch64
[00030.643] 01103.01117> dso: id=41ae135e613212ff base=0xe4fc1332c000 name=libunwind.so.1
[00030.643] 01103.01117> dso: id=9b65658a025840b080547ed536361c575fe8ea16 base=0xb2ac96fd6000 name=libsyslog.so
[00030.643] 01103.01117> dso: id=7e889d06ed38d270affccd2d9a879ac57ac7cd78 base=0xaee389f7b000 name=libasync-default.so
[00030.643] 01103.01117> dso: id=b5bdbbd9880b788b base=0xa45895cd5000 name=app:root_presenter
[00030.643] 01103.01117> dso: id=cb93b7e61e1cc5c4335dbd0492cac7ee66069543 base=0x9ec1d6904000 name=libc.so
[00030.643] 01103.01117> dso: id=0018089642a1bd2af1f4bb7b28197066fceedb74 base=0x8f7930146000 name=<vDSO>
[00030.643] 01103.01117> dso: id=b885824ac057347f base=0x8e2867628000 name=libc++abi.so.1
[00030.643] 01103.01117> dso: id=d61017d378402f6d base=0x74a3a852f000 name=libc++.so.2
[00030.643] 01103.01117> dso: id=3f1cc12a48e0ce54b3774ffceb3e3523e2db1095 base=0x724b49aeb000 name=libfdio.so
[00030.643] 01103.01117> dso: id=44bcee2c9f650c8641c5e23b76676c715e0eb235 base=0x50bdfd1b7000 name=libhid.so
[00030.643] 01103.01117> dso: id=fde08f43b850c7e7a51d12120d24a09d6fce85a5 base=0x4e1e21a1b000 name=libtrace-engine.so
[00030.643] 01103.01117> dso: id=251b7a82e8c01430 base=0x474a6551c000 name=libfxl.so
[00030.643] 01103.01117> dso: id=9202c12ca075a354 base=0x3d09f44b1000 name=libfsl.so
[00030.643] 01103.01117> dso: id=546a7506633a04ed base=0x2a4212b36000 name=libfxl_logging.so
[00030.643] 01103.01117> bt#01: pc 0xa45895d5ca18 sp 0x25638ad21bf0 (app:root_presenter,0x87a18)
[00030.644] 01103.01117> bt#02: pc 0xa45895d5c564 sp 0x25638ad21c70 (app:root_presenter,0x87564)
[00030.644] 01103.01117> bt#03: pc 0xa45895d5ddec sp 0x25638ad21cb0 (app:root_presenter,0x88dec)
[00030.644] 01103.01117> bt#04: pc 0xa45895d5dcec sp 0x25638ad21d10 (app:root_presenter,0x88cec)
[00030.645] 01103.01117> bt#05: pc 0xa45895d63004 sp 0x25638ad21d70 (app:root_presenter,0x8e004)
[00030.645] 01103.01117> bt#06: pc 0xa45895d62f64 sp 0x25638ad21dc0 (app:root_presenter,0x8df64)
[00030.645] 01103.01117> bt#07: pc 0xa45895dabb40 sp 0x25638ad21df0 (app:root_presenter,0xd6b40)
[00030.646] 01103.01117> bt#08: pc 0xa45895dab6e4 sp 0x25638ad21e20 (app:root_presenter,0xd66e4)
[00030.646] 01103.01117> bt#09: pc 0xa45895ddc0fc sp 0x25638ad21ea0 (app:root_presenter,0x1070fc)
[00030.646] 01103.01117> bt#10: pc 0xa45895ddc2a4 sp 0x25638ad21ed0 (app:root_presenter,0x1072a4)
[00030.647] 01103.01117> bt#11: pc 0xa45895ddc19c sp 0x25638ad21f20 (app:root_presenter,0x10719c)
[00030.647] 01103.01117> bt#12: pc 0xa45895d9550c sp 0x25638ad21f50 (app:root_presenter,0xc050c)
[00030.647] 01103.01117> bt#13: pc 0xa45895d4ef40 sp 0x25638ad21f90 (app:root_presenter,0x79f40)
[00030.647] 01103.01117> bt#14: pc 0x9ec1d691e0e4 sp 0x25638ad21fe0 (libc.so,0x1a0e4)
[00030.648] 01103.01117> bt#15: pc 0x9ec1d691e2f0 sp 0x25638ad22000 (libc.so,0x1a2f0)
[00030.648] 01103.01117> bt#16: end

start of symbolized stack:
#01: mozart::HidDecoder::ParseProtocol(mozart::HidDecoder::Protocol*) at ../../out/arm64/../../garnet/bin/ui/input_reader/hid_decoder.cc:202
#02: mozart::HidDecoder::Init() at ../../out/arm64/../../garnet/bin/ui/input_reader/hid_decoder.cc:75
#03: mozart::InputInterpreter::Initialize() at ../../out/arm64/../../garnet/bin/ui/input_reader/input_interpreter.cc:81
#04: mozart::InputInterpreter::Open(int, std::__2::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >, fuchsia::ui::input::InputDeviceRegistry*) at ../../out/arm64/../../garnet/bin/ui/input_reader/input_interpreter.cc:65
#05: operator() at ../../out/arm64/../../garnet/bin/ui/input_reader/input_reader.cc:35
#06: fit::internal::target<mozart::InputReader::Start()::$_0, true, void, int, std::__2::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> > >::invoke(void*, int, std::__2::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >) at ../../out/arm64/../../zircon/system/ulib/fit/include/lib/fit/function_internal.h:75
#07: fit::internal::function<16ul, false, void (int, std::__2::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >)>::operator()(int, std::__2::basic_string<char, std::__2::char_traits<char>, std::__2::allocator<char> >) const at ../../out/arm64/../../zircon/system/ulib/fit/include/lib/fit/function_internal.h:182
#08: fsl::DeviceWatcher::Handler(async_dispatcher*, async::WaitBase*, int, zx_packet_signal const*) at ../../out/arm64/../../garnet/public/lib/fsl/io/device_watcher.cc:86
#09: async_loop_dispatch_wait at ../../out/arm64/../../zircon/system/ulib/async-loop/loop.c:299
#10: async_loop_run_once at ../../out/arm64/../../zircon/system/ulib/async-loop/loop.c:256
#11: async_loop_run at ../../out/arm64/../../zircon/system/ulib/async-loop/loop.c:213
#12: async::Loop::Run(zx::basic_time<0u>, bool) at ../../out/arm64/../../zircon/system/ulib/async-loop/loop_wrapper.cpp:25
#13: main at ../../out/arm64/../../garnet/bin/ui/root_presenter/main.cc:22
#14: start_main at ../../zircon/third_party/ulib/musl/src/env/__libc_start_main.c:49
#15: __libc_start_main at ../../zircon/third_party/ulib/musl/src/env/__libc_start_main.c:170
end of symbolized stack
```

Any easy way to capture this output from the target is by running
the `loglistener` program on your development host.

### Manually requesting backtraces

Akin to printf debugging, one can request crashlogger print a
backtrace at a particular point in your code.

Include this header:

```
#include <zircon/crashlogger.h>
```

and then add the following where you want the backtrace printed:

```
void my_function() {
  ...
  crashlogger_request_backtrace();
  ...
}
```
