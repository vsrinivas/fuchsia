// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::collections::HashMap, uuid::Uuid};

lazy_static! {
    pub static ref TYPE_GUID_TO_NAME: HashMap<Uuid, &'static str> = {
        let mut m = HashMap::new();
        // Definitions taken from //zircon/system/public/zircon/hw/gpt.h.
        m.insert(Uuid::parse_str("FE8A2634-5E2E-46BA-99E3-3A192091A350").unwrap(), "bootloader");
        m.insert(Uuid::parse_str("D9FD4535-106C-4CEC-8D37-DFC020CA87CB").unwrap(), "durable");
        m.insert(Uuid::parse_str("A409E16B-78AA-4ACC-995C-302352621A41").unwrap(), "durable_boot");
        m.insert(Uuid::parse_str("F95D940E-CABA-4578-9B93-BB6C90F29D3E").unwrap(), "factory");
        m.insert(Uuid::parse_str("10B8DBAA-D2BF-42A9-98C6-A7C5DB3701E7").unwrap(), "factory_boot");
        m.insert(Uuid::parse_str("49FD7CB8-DF15-4E73-B9D9-992070127F0F").unwrap(), "fvm");
        m.insert(Uuid::parse_str("421A8BFC-85D9-4D85-ACDA-B64EEC0133E9").unwrap(), "vbmeta");
        m.insert(Uuid::parse_str("9B37FFF6-2E58-466A-983A-F7926D0B04E0").unwrap(), "zircon");
        m.insert(Uuid::parse_str("421A8BFC-85D9-4D85-ACDA-B64EEC0133E9").unwrap(), "vbmeta");
        // Legacy GUID definitions.
        m.insert(Uuid::parse_str("00000000-0000-0000-0000-000000000000").unwrap(), "empty");
        m.insert(Uuid::parse_str("C12A7328-F81F-11D2-BA4B-00A0C93EC93B").unwrap(), "fuchsia-esp");
        m.insert(Uuid::parse_str("606B000B-B7C7-4653-A7D5-B737332C899D").unwrap(), "fuchsia-system");
        m.insert(Uuid::parse_str("08185F0C-892D-428A-A789-DBEEC8F55E6A").unwrap(), "fuchsia-data");
        m.insert(Uuid::parse_str("48435546-4953-2041-494E-5354414C4C52").unwrap(), "fuchsia-install");
        m.insert(Uuid::parse_str("2967380E-134C-4CBB-B6DA-17E7CE1CA45D").unwrap(), "fuchsia-blob");
        m.insert(Uuid::parse_str("41D0E340-57E3-954E-8C1E-17ECAC44CFF5").unwrap(), "fuchsia-fvm");
        m.insert(Uuid::parse_str("DE30CC86-1F4A-4A31-93C4-66F147D33E05").unwrap(), "zircon-a");
        m.insert(Uuid::parse_str("23CC04DF-C278-4CE7-8471-897D1A4BCDF7").unwrap(), "zircon-b");
        m.insert(Uuid::parse_str("A0E5CF57-2DEF-46BE-A80C-A2067C37CD49").unwrap(), "zircon-r");
        m.insert(Uuid::parse_str("4E5E989E-4C86-11E8-A15B-480FCF35F8E6").unwrap(), "sys-config");
        m.insert(Uuid::parse_str("5A3A90BE-4C86-11E8-A15B-480FCF35F8E6").unwrap(), "facotry-config");
        m.insert(Uuid::parse_str("5ECE94FE-4C86-11E8-A15B-480FCF35F8E6").unwrap(), "bootloader");
        m.insert(Uuid::parse_str("8B94D043-30BE-4871-9DFA-D69556E8C1F3").unwrap(), "guid-test");
        m.insert(Uuid::parse_str("A13B4D9A-EC5F-11E8-97D8-6C3BE52705BF").unwrap(), "vbmeta_a");
        m.insert(Uuid::parse_str("A288ABF2-EC5F-11E8-97D8-6C3BE52705BF").unwrap(), "vbmeta_b");
        m.insert(Uuid::parse_str("6A2460C3-CD11-4E8B-80A8-12CCE268ED0A").unwrap(), "vbmeta_r");
        m.insert(Uuid::parse_str("1D75395D-F2C6-476B-A8B7-45CC1C97B476").unwrap(), "misc");
        m.insert(Uuid::parse_str("FE3A2A5D-4F32-41A7-B725-ACCC3285A309").unwrap(), "cros-kernel");
        m.insert(Uuid::parse_str("3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC").unwrap(), "cros-rootfs");
        m.insert(Uuid::parse_str("2E0A753D-9E48-43B0-8337-B15192CB1B5E").unwrap(), "cros-reserved");
        m.insert(Uuid::parse_str("CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3").unwrap(), "cros-firmware");
        m.insert(Uuid::parse_str("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7").unwrap(), "cros-data");
        m.insert(Uuid::parse_str("21686148-6449-6E6F-744E-656564454649").unwrap(), "bios");
        m.insert(Uuid::parse_str("900B0FC5-90CD-4D4F-84F9-9F8ED579DB88").unwrap(), "emmc-boot1");
        m.insert(Uuid::parse_str("B2B2E8D1-7C10-4EBC-A2D0-4614568260AD").unwrap(), "emmc-boot2");
        m.insert(Uuid::parse_str("0FC63DAF-8483-4772-8E79-3D69D8477DE4").unwrap(), "linux-filesystem");
        m
    };
}
