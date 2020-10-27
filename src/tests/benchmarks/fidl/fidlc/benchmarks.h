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

struct TestStruct {
int8 f1;
int8 f2;
int8 f3;
int8 f4;
int8 f5;
int8 f6;
int8 f7;
int8 f8;
int8 f9;
int8 f10;
int8 f11;
int8 f12;
int8 f13;
int8 f14;
int8 f15;
int8 f16;
int8 f17;
int8 f18;
int8 f19;
int8 f20;
int8 f21;
int8 f22;
int8 f23;
int8 f24;
int8 f25;
int8 f26;
int8 f27;
int8 f28;
int8 f29;
int8 f30;
int8 f31;
int8 f32;
int8 f33;
int8 f34;
int8 f35;
int8 f36;
int8 f37;
int8 f38;
int8 f39;
int8 f40;
int8 f41;
int8 f42;
int8 f43;
int8 f44;
int8 f45;
int8 f46;
int8 f47;
int8 f48;
int8 f49;
int8 f50;
int8 f51;
int8 f52;
int8 f53;
int8 f54;
int8 f55;
int8 f56;
int8 f57;
int8 f58;
int8 f59;
int8 f60;
int8 f61;
int8 f62;
int8 f63;
int8 f64;
};
)FIDL",
    },
    Benchmark{
        .name = "StructDeep/8",
        .fidl = R"FIDL(

library example;

struct TestStruct0 {
	int8 val;
};

struct TestStruct1 {
	TestStruct0 val;
};

struct TestStruct2 {
	TestStruct1 val;
};

struct TestStruct3 {
	TestStruct2 val;
};

struct TestStruct4 {
	TestStruct3 val;
};

struct TestStruct5 {
	TestStruct4 val;
};

struct TestStruct6 {
	TestStruct5 val;
};

struct TestStruct7 {
	TestStruct6 val;
};

struct TestStruct8 {
	TestStruct7 val;
};

)FIDL",
    },
    Benchmark{
        .name = "TableField/64",
        .fidl = R"FIDL(

library example;

table TestTable {
1: int8 f1;
2: int8 f2;
3: int8 f3;
4: int8 f4;
5: int8 f5;
6: int8 f6;
7: int8 f7;
8: int8 f8;
9: int8 f9;
10: int8 f10;
11: int8 f11;
12: int8 f12;
13: int8 f13;
14: int8 f14;
15: int8 f15;
16: int8 f16;
17: int8 f17;
18: int8 f18;
19: int8 f19;
20: int8 f20;
21: int8 f21;
22: int8 f22;
23: int8 f23;
24: int8 f24;
25: int8 f25;
26: int8 f26;
27: int8 f27;
28: int8 f28;
29: int8 f29;
30: int8 f30;
31: int8 f31;
32: int8 f32;
33: int8 f33;
34: int8 f34;
35: int8 f35;
36: int8 f36;
37: int8 f37;
38: int8 f38;
39: int8 f39;
40: int8 f40;
41: int8 f41;
42: int8 f42;
43: int8 f43;
44: int8 f44;
45: int8 f45;
46: int8 f46;
47: int8 f47;
48: int8 f48;
49: int8 f49;
50: int8 f50;
51: int8 f51;
52: int8 f52;
53: int8 f53;
54: int8 f54;
55: int8 f55;
56: int8 f56;
57: int8 f57;
58: int8 f58;
59: int8 f59;
60: int8 f60;
61: int8 f61;
62: int8 f62;
63: int8 f63;
64: int8 f64;
};
)FIDL",
    },
    Benchmark{
        .name = "TableDeep/64",
        .fidl = R"FIDL(

library example;

table TestTable0 {
	1: int8 val;
};

table TestTable1 {
	1: TestTable0 val;
};

table TestTable2 {
	1: TestTable1 val;
};

table TestTable3 {
	1: TestTable2 val;
};

table TestTable4 {
	1: TestTable3 val;
};

table TestTable5 {
	1: TestTable4 val;
};

table TestTable6 {
	1: TestTable5 val;
};

table TestTable7 {
	1: TestTable6 val;
};

table TestTable8 {
	1: TestTable7 val;
};

table TestTable9 {
	1: TestTable8 val;
};

table TestTable10 {
	1: TestTable9 val;
};

table TestTable11 {
	1: TestTable10 val;
};

table TestTable12 {
	1: TestTable11 val;
};

table TestTable13 {
	1: TestTable12 val;
};

table TestTable14 {
	1: TestTable13 val;
};

table TestTable15 {
	1: TestTable14 val;
};

table TestTable16 {
	1: TestTable15 val;
};

table TestTable17 {
	1: TestTable16 val;
};

table TestTable18 {
	1: TestTable17 val;
};

table TestTable19 {
	1: TestTable18 val;
};

table TestTable20 {
	1: TestTable19 val;
};

table TestTable21 {
	1: TestTable20 val;
};

table TestTable22 {
	1: TestTable21 val;
};

table TestTable23 {
	1: TestTable22 val;
};

table TestTable24 {
	1: TestTable23 val;
};

table TestTable25 {
	1: TestTable24 val;
};

table TestTable26 {
	1: TestTable25 val;
};

table TestTable27 {
	1: TestTable26 val;
};

table TestTable28 {
	1: TestTable27 val;
};

table TestTable29 {
	1: TestTable28 val;
};

table TestTable30 {
	1: TestTable29 val;
};

table TestTable31 {
	1: TestTable30 val;
};

table TestTable32 {
	1: TestTable31 val;
};

table TestTable33 {
	1: TestTable32 val;
};

table TestTable34 {
	1: TestTable33 val;
};

table TestTable35 {
	1: TestTable34 val;
};

table TestTable36 {
	1: TestTable35 val;
};

table TestTable37 {
	1: TestTable36 val;
};

table TestTable38 {
	1: TestTable37 val;
};

table TestTable39 {
	1: TestTable38 val;
};

table TestTable40 {
	1: TestTable39 val;
};

table TestTable41 {
	1: TestTable40 val;
};

table TestTable42 {
	1: TestTable41 val;
};

table TestTable43 {
	1: TestTable42 val;
};

table TestTable44 {
	1: TestTable43 val;
};

table TestTable45 {
	1: TestTable44 val;
};

table TestTable46 {
	1: TestTable45 val;
};

table TestTable47 {
	1: TestTable46 val;
};

table TestTable48 {
	1: TestTable47 val;
};

table TestTable49 {
	1: TestTable48 val;
};

table TestTable50 {
	1: TestTable49 val;
};

table TestTable51 {
	1: TestTable50 val;
};

table TestTable52 {
	1: TestTable51 val;
};

table TestTable53 {
	1: TestTable52 val;
};

table TestTable54 {
	1: TestTable53 val;
};

table TestTable55 {
	1: TestTable54 val;
};

table TestTable56 {
	1: TestTable55 val;
};

table TestTable57 {
	1: TestTable56 val;
};

table TestTable58 {
	1: TestTable57 val;
};

table TestTable59 {
	1: TestTable58 val;
};

table TestTable60 {
	1: TestTable59 val;
};

table TestTable61 {
	1: TestTable60 val;
};

table TestTable62 {
	1: TestTable61 val;
};

table TestTable63 {
	1: TestTable62 val;
};

table TestTable64 {
	1: TestTable63 val;
};

)FIDL",
    },
    Benchmark{
        .name = "UnionField/64",
        .fidl = R"FIDL(

library example;

union TestUnion {
1: int8 f1;
2: int8 f2;
3: int8 f3;
4: int8 f4;
5: int8 f5;
6: int8 f6;
7: int8 f7;
8: int8 f8;
9: int8 f9;
10: int8 f10;
11: int8 f11;
12: int8 f12;
13: int8 f13;
14: int8 f14;
15: int8 f15;
16: int8 f16;
17: int8 f17;
18: int8 f18;
19: int8 f19;
20: int8 f20;
21: int8 f21;
22: int8 f22;
23: int8 f23;
24: int8 f24;
25: int8 f25;
26: int8 f26;
27: int8 f27;
28: int8 f28;
29: int8 f29;
30: int8 f30;
31: int8 f31;
32: int8 f32;
33: int8 f33;
34: int8 f34;
35: int8 f35;
36: int8 f36;
37: int8 f37;
38: int8 f38;
39: int8 f39;
40: int8 f40;
41: int8 f41;
42: int8 f42;
43: int8 f43;
44: int8 f44;
45: int8 f45;
46: int8 f46;
47: int8 f47;
48: int8 f48;
49: int8 f49;
50: int8 f50;
51: int8 f51;
52: int8 f52;
53: int8 f53;
54: int8 f54;
55: int8 f55;
56: int8 f56;
57: int8 f57;
58: int8 f58;
59: int8 f59;
60: int8 f60;
61: int8 f61;
62: int8 f62;
63: int8 f63;
64: int8 f64;
};
)FIDL",
    },
    Benchmark{
        .name = "UnionDeep/64",
        .fidl = R"FIDL(

library example;

union TestUnion0 {
	1: int8 val;
};

union TestUnion1 {
	1: TestUnion0 val;
};

union TestUnion2 {
	1: TestUnion1 val;
};

union TestUnion3 {
	1: TestUnion2 val;
};

union TestUnion4 {
	1: TestUnion3 val;
};

union TestUnion5 {
	1: TestUnion4 val;
};

union TestUnion6 {
	1: TestUnion5 val;
};

union TestUnion7 {
	1: TestUnion6 val;
};

union TestUnion8 {
	1: TestUnion7 val;
};

union TestUnion9 {
	1: TestUnion8 val;
};

union TestUnion10 {
	1: TestUnion9 val;
};

union TestUnion11 {
	1: TestUnion10 val;
};

union TestUnion12 {
	1: TestUnion11 val;
};

union TestUnion13 {
	1: TestUnion12 val;
};

union TestUnion14 {
	1: TestUnion13 val;
};

union TestUnion15 {
	1: TestUnion14 val;
};

union TestUnion16 {
	1: TestUnion15 val;
};

union TestUnion17 {
	1: TestUnion16 val;
};

union TestUnion18 {
	1: TestUnion17 val;
};

union TestUnion19 {
	1: TestUnion18 val;
};

union TestUnion20 {
	1: TestUnion19 val;
};

union TestUnion21 {
	1: TestUnion20 val;
};

union TestUnion22 {
	1: TestUnion21 val;
};

union TestUnion23 {
	1: TestUnion22 val;
};

union TestUnion24 {
	1: TestUnion23 val;
};

union TestUnion25 {
	1: TestUnion24 val;
};

union TestUnion26 {
	1: TestUnion25 val;
};

union TestUnion27 {
	1: TestUnion26 val;
};

union TestUnion28 {
	1: TestUnion27 val;
};

union TestUnion29 {
	1: TestUnion28 val;
};

union TestUnion30 {
	1: TestUnion29 val;
};

union TestUnion31 {
	1: TestUnion30 val;
};

union TestUnion32 {
	1: TestUnion31 val;
};

union TestUnion33 {
	1: TestUnion32 val;
};

union TestUnion34 {
	1: TestUnion33 val;
};

union TestUnion35 {
	1: TestUnion34 val;
};

union TestUnion36 {
	1: TestUnion35 val;
};

union TestUnion37 {
	1: TestUnion36 val;
};

union TestUnion38 {
	1: TestUnion37 val;
};

union TestUnion39 {
	1: TestUnion38 val;
};

union TestUnion40 {
	1: TestUnion39 val;
};

union TestUnion41 {
	1: TestUnion40 val;
};

union TestUnion42 {
	1: TestUnion41 val;
};

union TestUnion43 {
	1: TestUnion42 val;
};

union TestUnion44 {
	1: TestUnion43 val;
};

union TestUnion45 {
	1: TestUnion44 val;
};

union TestUnion46 {
	1: TestUnion45 val;
};

union TestUnion47 {
	1: TestUnion46 val;
};

union TestUnion48 {
	1: TestUnion47 val;
};

union TestUnion49 {
	1: TestUnion48 val;
};

union TestUnion50 {
	1: TestUnion49 val;
};

union TestUnion51 {
	1: TestUnion50 val;
};

union TestUnion52 {
	1: TestUnion51 val;
};

union TestUnion53 {
	1: TestUnion52 val;
};

union TestUnion54 {
	1: TestUnion53 val;
};

union TestUnion55 {
	1: TestUnion54 val;
};

union TestUnion56 {
	1: TestUnion55 val;
};

union TestUnion57 {
	1: TestUnion56 val;
};

union TestUnion58 {
	1: TestUnion57 val;
};

union TestUnion59 {
	1: TestUnion58 val;
};

union TestUnion60 {
	1: TestUnion59 val;
};

union TestUnion61 {
	1: TestUnion60 val;
};

union TestUnion62 {
	1: TestUnion61 val;
};

union TestUnion63 {
	1: TestUnion62 val;
};

union TestUnion64 {
	1: TestUnion63 val;
};

)FIDL",
    },
};
