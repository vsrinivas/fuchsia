// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated. To regenerate, run:
// $FUCHSIA_DIR/src/tests/benchmarks/fidl/fidlc/regen.sh

struct Benchmark {
  const char* name;
  const char* fidl;
};

constexpr Benchmark benchmarks[] = {
    Benchmark{
        .name = "StructField/64",
        .fidl = R"FIDL(

library example;

type TestStruct = struct {
f1 int8;
f2 int8;
f3 int8;
f4 int8;
f5 int8;
f6 int8;
f7 int8;
f8 int8;
f9 int8;
f10 int8;
f11 int8;
f12 int8;
f13 int8;
f14 int8;
f15 int8;
f16 int8;
f17 int8;
f18 int8;
f19 int8;
f20 int8;
f21 int8;
f22 int8;
f23 int8;
f24 int8;
f25 int8;
f26 int8;
f27 int8;
f28 int8;
f29 int8;
f30 int8;
f31 int8;
f32 int8;
f33 int8;
f34 int8;
f35 int8;
f36 int8;
f37 int8;
f38 int8;
f39 int8;
f40 int8;
f41 int8;
f42 int8;
f43 int8;
f44 int8;
f45 int8;
f46 int8;
f47 int8;
f48 int8;
f49 int8;
f50 int8;
f51 int8;
f52 int8;
f53 int8;
f54 int8;
f55 int8;
f56 int8;
f57 int8;
f58 int8;
f59 int8;
f60 int8;
f61 int8;
f62 int8;
f63 int8;
f64 int8;
};
)FIDL",
    },
    Benchmark{
        .name = "StructDeep/8",
        .fidl = R"FIDL(

library example;

type TestStruct0 = struct {
	val int8;
};

type TestStruct1 = struct {
	val TestStruct0;
};

type TestStruct2 = struct {
	val TestStruct1;
};

type TestStruct3 = struct {
	val TestStruct2;
};

type TestStruct4 = struct {
	val TestStruct3;
};

type TestStruct5 = struct {
	val TestStruct4;
};

type TestStruct6 = struct {
	val TestStruct5;
};

type TestStruct7 = struct {
	val TestStruct6;
};

type TestStruct8 = struct {
	val TestStruct7;
};

)FIDL",
    },
    Benchmark{
        .name = "TableField/64",
        .fidl = R"FIDL(

library example;

type TestTable = table {
1: f1 int8;
2: f2 int8;
3: f3 int8;
4: f4 int8;
5: f5 int8;
6: f6 int8;
7: f7 int8;
8: f8 int8;
9: f9 int8;
10: f10 int8;
11: f11 int8;
12: f12 int8;
13: f13 int8;
14: f14 int8;
15: f15 int8;
16: f16 int8;
17: f17 int8;
18: f18 int8;
19: f19 int8;
20: f20 int8;
21: f21 int8;
22: f22 int8;
23: f23 int8;
24: f24 int8;
25: f25 int8;
26: f26 int8;
27: f27 int8;
28: f28 int8;
29: f29 int8;
30: f30 int8;
31: f31 int8;
32: f32 int8;
33: f33 int8;
34: f34 int8;
35: f35 int8;
36: f36 int8;
37: f37 int8;
38: f38 int8;
39: f39 int8;
40: f40 int8;
41: f41 int8;
42: f42 int8;
43: f43 int8;
44: f44 int8;
45: f45 int8;
46: f46 int8;
47: f47 int8;
48: f48 int8;
49: f49 int8;
50: f50 int8;
51: f51 int8;
52: f52 int8;
53: f53 int8;
54: f54 int8;
55: f55 int8;
56: f56 int8;
57: f57 int8;
58: f58 int8;
59: f59 int8;
60: f60 int8;
61: f61 int8;
62: f62 int8;
63: f63 int8;
64: f64 int8;
};
)FIDL",
    },
    Benchmark{
        .name = "TableDeep/64",
        .fidl = R"FIDL(

library example;

type TestTable0 = table {
	1: val int8;
};

type TestTable1 = table {
	1: val TestTable0;
};

type TestTable2 = table {
	1: val TestTable1;
};

type TestTable3 = table {
	1: val TestTable2;
};

type TestTable4 = table {
	1: val TestTable3;
};

type TestTable5 = table {
	1: val TestTable4;
};

type TestTable6 = table {
	1: val TestTable5;
};

type TestTable7 = table {
	1: val TestTable6;
};

type TestTable8 = table {
	1: val TestTable7;
};

type TestTable9 = table {
	1: val TestTable8;
};

type TestTable10 = table {
	1: val TestTable9;
};

type TestTable11 = table {
	1: val TestTable10;
};

type TestTable12 = table {
	1: val TestTable11;
};

type TestTable13 = table {
	1: val TestTable12;
};

type TestTable14 = table {
	1: val TestTable13;
};

type TestTable15 = table {
	1: val TestTable14;
};

type TestTable16 = table {
	1: val TestTable15;
};

type TestTable17 = table {
	1: val TestTable16;
};

type TestTable18 = table {
	1: val TestTable17;
};

type TestTable19 = table {
	1: val TestTable18;
};

type TestTable20 = table {
	1: val TestTable19;
};

type TestTable21 = table {
	1: val TestTable20;
};

type TestTable22 = table {
	1: val TestTable21;
};

type TestTable23 = table {
	1: val TestTable22;
};

type TestTable24 = table {
	1: val TestTable23;
};

type TestTable25 = table {
	1: val TestTable24;
};

type TestTable26 = table {
	1: val TestTable25;
};

type TestTable27 = table {
	1: val TestTable26;
};

type TestTable28 = table {
	1: val TestTable27;
};

type TestTable29 = table {
	1: val TestTable28;
};

type TestTable30 = table {
	1: val TestTable29;
};

type TestTable31 = table {
	1: val TestTable30;
};

type TestTable32 = table {
	1: val TestTable31;
};

type TestTable33 = table {
	1: val TestTable32;
};

type TestTable34 = table {
	1: val TestTable33;
};

type TestTable35 = table {
	1: val TestTable34;
};

type TestTable36 = table {
	1: val TestTable35;
};

type TestTable37 = table {
	1: val TestTable36;
};

type TestTable38 = table {
	1: val TestTable37;
};

type TestTable39 = table {
	1: val TestTable38;
};

type TestTable40 = table {
	1: val TestTable39;
};

type TestTable41 = table {
	1: val TestTable40;
};

type TestTable42 = table {
	1: val TestTable41;
};

type TestTable43 = table {
	1: val TestTable42;
};

type TestTable44 = table {
	1: val TestTable43;
};

type TestTable45 = table {
	1: val TestTable44;
};

type TestTable46 = table {
	1: val TestTable45;
};

type TestTable47 = table {
	1: val TestTable46;
};

type TestTable48 = table {
	1: val TestTable47;
};

type TestTable49 = table {
	1: val TestTable48;
};

type TestTable50 = table {
	1: val TestTable49;
};

type TestTable51 = table {
	1: val TestTable50;
};

type TestTable52 = table {
	1: val TestTable51;
};

type TestTable53 = table {
	1: val TestTable52;
};

type TestTable54 = table {
	1: val TestTable53;
};

type TestTable55 = table {
	1: val TestTable54;
};

type TestTable56 = table {
	1: val TestTable55;
};

type TestTable57 = table {
	1: val TestTable56;
};

type TestTable58 = table {
	1: val TestTable57;
};

type TestTable59 = table {
	1: val TestTable58;
};

type TestTable60 = table {
	1: val TestTable59;
};

type TestTable61 = table {
	1: val TestTable60;
};

type TestTable62 = table {
	1: val TestTable61;
};

type TestTable63 = table {
	1: val TestTable62;
};

type TestTable64 = table {
	1: val TestTable63;
};

)FIDL",
    },
    Benchmark{
        .name = "UnionField/64",
        .fidl = R"FIDL(

library example;

type TestUnion = union {
1: f1 int8;
2: f2 int8;
3: f3 int8;
4: f4 int8;
5: f5 int8;
6: f6 int8;
7: f7 int8;
8: f8 int8;
9: f9 int8;
10: f10 int8;
11: f11 int8;
12: f12 int8;
13: f13 int8;
14: f14 int8;
15: f15 int8;
16: f16 int8;
17: f17 int8;
18: f18 int8;
19: f19 int8;
20: f20 int8;
21: f21 int8;
22: f22 int8;
23: f23 int8;
24: f24 int8;
25: f25 int8;
26: f26 int8;
27: f27 int8;
28: f28 int8;
29: f29 int8;
30: f30 int8;
31: f31 int8;
32: f32 int8;
33: f33 int8;
34: f34 int8;
35: f35 int8;
36: f36 int8;
37: f37 int8;
38: f38 int8;
39: f39 int8;
40: f40 int8;
41: f41 int8;
42: f42 int8;
43: f43 int8;
44: f44 int8;
45: f45 int8;
46: f46 int8;
47: f47 int8;
48: f48 int8;
49: f49 int8;
50: f50 int8;
51: f51 int8;
52: f52 int8;
53: f53 int8;
54: f54 int8;
55: f55 int8;
56: f56 int8;
57: f57 int8;
58: f58 int8;
59: f59 int8;
60: f60 int8;
61: f61 int8;
62: f62 int8;
63: f63 int8;
64: f64 int8;
};
)FIDL",
    },
    Benchmark{
        .name = "UnionDeep/64",
        .fidl = R"FIDL(

library example;

type TestUnion0 = union {
	1: val int8;
};

type TestUnion1 = union {
	1: val TestUnion0;
};

type TestUnion2 = union {
	1: val TestUnion1;
};

type TestUnion3 = union {
	1: val TestUnion2;
};

type TestUnion4 = union {
	1: val TestUnion3;
};

type TestUnion5 = union {
	1: val TestUnion4;
};

type TestUnion6 = union {
	1: val TestUnion5;
};

type TestUnion7 = union {
	1: val TestUnion6;
};

type TestUnion8 = union {
	1: val TestUnion7;
};

type TestUnion9 = union {
	1: val TestUnion8;
};

type TestUnion10 = union {
	1: val TestUnion9;
};

type TestUnion11 = union {
	1: val TestUnion10;
};

type TestUnion12 = union {
	1: val TestUnion11;
};

type TestUnion13 = union {
	1: val TestUnion12;
};

type TestUnion14 = union {
	1: val TestUnion13;
};

type TestUnion15 = union {
	1: val TestUnion14;
};

type TestUnion16 = union {
	1: val TestUnion15;
};

type TestUnion17 = union {
	1: val TestUnion16;
};

type TestUnion18 = union {
	1: val TestUnion17;
};

type TestUnion19 = union {
	1: val TestUnion18;
};

type TestUnion20 = union {
	1: val TestUnion19;
};

type TestUnion21 = union {
	1: val TestUnion20;
};

type TestUnion22 = union {
	1: val TestUnion21;
};

type TestUnion23 = union {
	1: val TestUnion22;
};

type TestUnion24 = union {
	1: val TestUnion23;
};

type TestUnion25 = union {
	1: val TestUnion24;
};

type TestUnion26 = union {
	1: val TestUnion25;
};

type TestUnion27 = union {
	1: val TestUnion26;
};

type TestUnion28 = union {
	1: val TestUnion27;
};

type TestUnion29 = union {
	1: val TestUnion28;
};

type TestUnion30 = union {
	1: val TestUnion29;
};

type TestUnion31 = union {
	1: val TestUnion30;
};

type TestUnion32 = union {
	1: val TestUnion31;
};

type TestUnion33 = union {
	1: val TestUnion32;
};

type TestUnion34 = union {
	1: val TestUnion33;
};

type TestUnion35 = union {
	1: val TestUnion34;
};

type TestUnion36 = union {
	1: val TestUnion35;
};

type TestUnion37 = union {
	1: val TestUnion36;
};

type TestUnion38 = union {
	1: val TestUnion37;
};

type TestUnion39 = union {
	1: val TestUnion38;
};

type TestUnion40 = union {
	1: val TestUnion39;
};

type TestUnion41 = union {
	1: val TestUnion40;
};

type TestUnion42 = union {
	1: val TestUnion41;
};

type TestUnion43 = union {
	1: val TestUnion42;
};

type TestUnion44 = union {
	1: val TestUnion43;
};

type TestUnion45 = union {
	1: val TestUnion44;
};

type TestUnion46 = union {
	1: val TestUnion45;
};

type TestUnion47 = union {
	1: val TestUnion46;
};

type TestUnion48 = union {
	1: val TestUnion47;
};

type TestUnion49 = union {
	1: val TestUnion48;
};

type TestUnion50 = union {
	1: val TestUnion49;
};

type TestUnion51 = union {
	1: val TestUnion50;
};

type TestUnion52 = union {
	1: val TestUnion51;
};

type TestUnion53 = union {
	1: val TestUnion52;
};

type TestUnion54 = union {
	1: val TestUnion53;
};

type TestUnion55 = union {
	1: val TestUnion54;
};

type TestUnion56 = union {
	1: val TestUnion55;
};

type TestUnion57 = union {
	1: val TestUnion56;
};

type TestUnion58 = union {
	1: val TestUnion57;
};

type TestUnion59 = union {
	1: val TestUnion58;
};

type TestUnion60 = union {
	1: val TestUnion59;
};

type TestUnion61 = union {
	1: val TestUnion60;
};

type TestUnion62 = union {
	1: val TestUnion61;
};

type TestUnion63 = union {
	1: val TestUnion62;
};

type TestUnion64 = union {
	1: val TestUnion63;
};

)FIDL",
    },
};
