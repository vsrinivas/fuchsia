// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func compileEnums(r fidlgen.Root) map[string]*Enum {
	ir := compile(r)
	enums := make(map[string]*Enum)
	for _, v := range ir.Decls {
		e := v.(*Enum)
		enums[e.Wire.Name()] = e
	}
	return enums
}

func TestUnusedValueInUnsignedEnum(t *testing.T) {
	enums := compileEnums(fidlgentest.EndToEndTest{T: t}.Single(`
library example;

type E8 = enum : uint8 {
	A = 0;
	B = 1;
};

type E16 = enum : uint16 {
	A = 0;
	B = 1;
};

type E32 = enum : uint32 {
	A = 0;
	B = 1;
};

type E64 = enum : uint64 {
	A = 0;
	B = 1;
};
`))

	assertEqual(t, enums["E8"].UnusedEnumValueForTmpl(), uint64(0xff))
	assertEqual(t, enums["E16"].UnusedEnumValueForTmpl(), uint64(0xffff))
	assertEqual(t, enums["E32"].UnusedEnumValueForTmpl(), uint64(0xffffffff))
	assertEqual(t, enums["E64"].UnusedEnumValueForTmpl(), uint64(0xffffffffffffffff))
}

func TestUnusedValueInUnsignedEnumUnknown(t *testing.T) {
	enums := compileEnums(fidlgentest.EndToEndTest{T: t}.Single(`
library example;

type E8 = enum : uint8 {
	A = 0;
	@unknown
	B = 1;
	C = 0xff;
};

type E16 = enum : uint16 {
	A = 0;
	@unknown
	B = 1;
	C = 0xffff;
};

type E32 = enum : uint32 {
	A = 0;
	@unknown
	B = 1;
	C = 0xffffffff;
};

type E64 = enum : uint64 {
	A = 0;
	@unknown
	B = 1;
	C = 0xffffffffffffffff;
};
`))

	assertEqual(t, enums["E8"].UnusedEnumValueForTmpl(), uint64(0xff-1))
	assertEqual(t, enums["E16"].UnusedEnumValueForTmpl(), uint64(0xffff-1))
	assertEqual(t, enums["E32"].UnusedEnumValueForTmpl(), uint64(0xffffffff-1))
	assertEqual(t, enums["E64"].UnusedEnumValueForTmpl(), uint64(0xffffffffffffffff-1))
}

func TestUnusedValueInSignedEnum(t *testing.T) {
	enums := compileEnums(fidlgentest.EndToEndTest{T: t}.Single(`
library example;

type E8 = enum : int8 {
	A = 0;
	B = 1;
};

type E16 = enum : int16 {
	A = 0;
	B = 1;
};

type E32 = enum : int32 {
	A = 0;
	B = 1;
};

type E64 = enum : int64 {
	A = 0;
	B = 1;
};
`))

	assertEqual(t, enums["E8"].UnusedEnumValueForTmpl(), uint64(0x7f))
	assertEqual(t, enums["E16"].UnusedEnumValueForTmpl(), uint64(0x7fff))
	assertEqual(t, enums["E32"].UnusedEnumValueForTmpl(), uint64(0x7fffffff))
	assertEqual(t, enums["E64"].UnusedEnumValueForTmpl(), uint64(0x7fffffffffffffff))
}

func TestUnusedValueInSignedEnumUnknown(t *testing.T) {
	enums := compileEnums(fidlgentest.EndToEndTest{T: t}.Single(`
library example;

type E8 = enum : int8 {
	A = 0;
	@unknown
	B = 1;
	C = 0x7f;
};

type E16 = enum : int16 {
	A = 0;
	@unknown
	B = 1;
	C = 0x7fff;
};

type E32 = enum : int32 {
	A = 0;
	@unknown
	B = 1;
	C = 0x7fffffff;
};

type E64 = enum : int64 {
	A = 0;
	@unknown
	B = 1;
	C = 0x7fffffffffffffff;
};
`))

	assertEqual(t, enums["E8"].UnusedEnumValueForTmpl(), uint64(0x7f-1))
	assertEqual(t, enums["E16"].UnusedEnumValueForTmpl(), uint64(0x7fff-1))
	assertEqual(t, enums["E32"].UnusedEnumValueForTmpl(), uint64(0x7fffffff-1))
	assertEqual(t, enums["E64"].UnusedEnumValueForTmpl(), uint64(0x7fffffffffffffff-1))
}

func TestUnusedValueInEnumFullyPopulated(t *testing.T) {
	enums := compileEnums(fidlgentest.EndToEndTest{T: t}.Single(`
library example;

type E8 = enum : uint8 {
	V0 = 0;

	@unknown
	V1 = 1;

	V2 = 2;
	V3 = 3;
	V4 = 4;
	V5 = 5;
	V6 = 6;
	V7 = 7;
	V8 = 8;
	V9 = 9;
	V10 = 10;
	V11 = 11;
	V12 = 12;
	V13 = 13;
	V14 = 14;
	V15 = 15;
	V16 = 16;
	V17 = 17;
	V18 = 18;
	V19 = 19;
	V20 = 20;
	V21 = 21;
	V22 = 22;
	V23 = 23;
	V24 = 24;
	V25 = 25;
	V26 = 26;
	V27 = 27;
	V28 = 28;
	V29 = 29;
	V30 = 30;
	V31 = 31;
	V32 = 32;
	V33 = 33;
	V34 = 34;
	V35 = 35;
	V36 = 36;
	V37 = 37;
	V38 = 38;
	V39 = 39;
	V40 = 40;
	V41 = 41;
	V42 = 42;
	V43 = 43;
	V44 = 44;
	V45 = 45;
	V46 = 46;
	V47 = 47;
	V48 = 48;
	V49 = 49;
	V50 = 50;
	V51 = 51;
	V52 = 52;
	V53 = 53;
	V54 = 54;
	V55 = 55;
	V56 = 56;
	V57 = 57;
	V58 = 58;
	V59 = 59;
	V60 = 60;
	V61 = 61;
	V62 = 62;
	V63 = 63;
	V64 = 64;
	V65 = 65;
	V66 = 66;
	V67 = 67;
	V68 = 68;
	V69 = 69;
	V70 = 70;
	V71 = 71;
	V72 = 72;
	V73 = 73;
	V74 = 74;
	V75 = 75;
	V76 = 76;
	V77 = 77;
	V78 = 78;
	V79 = 79;
	V80 = 80;
	V81 = 81;
	V82 = 82;
	V83 = 83;
	V84 = 84;
	V85 = 85;
	V86 = 86;
	V87 = 87;
	V88 = 88;
	V89 = 89;
	V90 = 90;
	V91 = 91;
	V92 = 92;
	V93 = 93;
	V94 = 94;
	V95 = 95;
	V96 = 96;
	V97 = 97;
	V98 = 98;
	V99 = 99;
	V100 = 100;
	V101 = 101;
	V102 = 102;
	V103 = 103;
	V104 = 104;
	V105 = 105;
	V106 = 106;
	V107 = 107;
	V108 = 108;
	V109 = 109;
	V110 = 110;
	V111 = 111;
	V112 = 112;
	V113 = 113;
	V114 = 114;
	V115 = 115;
	V116 = 116;
	V117 = 117;
	V118 = 118;
	V119 = 119;
	V120 = 120;
	V121 = 121;
	V122 = 122;
	V123 = 123;
	V124 = 124;
	V125 = 125;
	V126 = 126;
	V127 = 127;
	V128 = 128;
	V129 = 129;
	V130 = 130;
	V131 = 131;
	V132 = 132;
	V133 = 133;
	V134 = 134;
	V135 = 135;
	V136 = 136;
	V137 = 137;
	V138 = 138;
	V139 = 139;
	V140 = 140;
	V141 = 141;
	V142 = 142;
	V143 = 143;
	V144 = 144;
	V145 = 145;
	V146 = 146;
	V147 = 147;
	V148 = 148;
	V149 = 149;
	V150 = 150;
	V151 = 151;
	V152 = 152;
	V153 = 153;
	V154 = 154;
	V155 = 155;
	V156 = 156;
	V157 = 157;
	V158 = 158;
	V159 = 159;
	V160 = 160;
	V161 = 161;
	V162 = 162;
	V163 = 163;
	V164 = 164;
	V165 = 165;
	V166 = 166;
	V167 = 167;
	V168 = 168;
	V169 = 169;
	V170 = 170;
	V171 = 171;
	V172 = 172;
	V173 = 173;
	V174 = 174;
	V175 = 175;
	V176 = 176;
	V177 = 177;
	V178 = 178;
	V179 = 179;
	V180 = 180;
	V181 = 181;
	V182 = 182;
	V183 = 183;
	V184 = 184;
	V185 = 185;
	V186 = 186;
	V187 = 187;
	V188 = 188;
	V189 = 189;
	V190 = 190;
	V191 = 191;
	V192 = 192;
	V193 = 193;
	V194 = 194;
	V195 = 195;
	V196 = 196;
	V197 = 197;
	V198 = 198;
	V199 = 199;
	V200 = 200;
	V201 = 201;
	V202 = 202;
	V203 = 203;
	V204 = 204;
	V205 = 205;
	V206 = 206;
	V207 = 207;
	V208 = 208;
	V209 = 209;
	V210 = 210;
	V211 = 211;
	V212 = 212;
	V213 = 213;
	V214 = 214;
	V215 = 215;
	V216 = 216;
	V217 = 217;
	V218 = 218;
	V219 = 219;
	V220 = 220;
	V221 = 221;
	V222 = 222;
	V223 = 223;
	V224 = 224;
	V225 = 225;
	V226 = 226;
	V227 = 227;
	V228 = 228;
	V229 = 229;
	V230 = 230;
	V231 = 231;
	V232 = 232;
	V233 = 233;
	V234 = 234;
	V235 = 235;
	V236 = 236;
	V237 = 237;
	V238 = 238;
	V239 = 239;
	V240 = 240;
	V241 = 241;
	V242 = 242;
	V243 = 243;
	V244 = 244;
	V245 = 245;
	V246 = 246;
	V247 = 247;
	V248 = 248;
	V249 = 249;
	V250 = 250;
	V251 = 251;
	V252 = 252;
	V253 = 253;
	V254 = 254;
	V255 = 255;
};
`))

	assertEqual(t, enums["E8"].UnusedEnumValueForTmpl(), uint64(1))
}
