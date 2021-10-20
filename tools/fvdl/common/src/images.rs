// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Result},
    errors::{ffx_bail, ffx_error},
    serde::Deserialize,
    std::{fs, io::BufReader},
};

#[derive(Default, Deserialize)]
pub struct Images(Vec<Image>);

#[derive(Default, Deserialize)]
pub struct Image {
    pub name: String,
    pub path: String,
    #[serde(rename = "type")]
    pub image_type: String,

    #[serde(default)]
    pub archive: bool,
    #[serde(default)]
    pub bootserver_pave: Vec<String>,
    #[serde(default)]
    pub bootserver_pave_zedboot: Vec<String>,
    #[serde(default)]
    pub fastboot_flash: Vec<String>,
    #[serde(default)]
    pub cpu: Option<String>,
    #[serde(default)]
    pub mkzedboot_mode: Vec<String>,
    #[serde(default)]
    pub compressed: bool,
    #[serde(default)]
    pub testonly: bool,
    #[serde(default)]
    pub label: Option<String>,
}

impl Images {
    pub fn from_build_dir(path: std::path::PathBuf) -> Result<Self> {
        let manifest_path = path.join("images.json");
        fs::File::open(manifest_path.clone())
            .map_err(|e| ffx_error!("Cannot open file {:?} \nerror: {:?}", manifest_path, e))
            .map(BufReader::new)
            .map(serde_json::from_reader)?
            .map_err(|e| anyhow!("json parsing errored {}", e))
    }

    #[cfg(test)]
    pub fn from_string(content: &str) -> Result<Self> {
        serde_json::from_str(content).map_err(|e| anyhow!("json parsing errored {}", e))
    }

    /// Finds the first matching artifact from images.json in the order of the values in names
    ///
    /// # Arguments
    ///
    /// * `names` - An array containing the artifact names to search from image.json. The first match
    ///    will be returned.
    /// * `image_type` - Image type such as "blk", "zbi" used to match artifact.
    pub fn find_path(&self, names: Vec<&str>, image_type: &str) -> Result<String> {
        for name in names.iter() {
            for image in self.0.iter() {
                if image.name == name.to_owned() && image.image_type == image_type {
                    return Ok(image.path.clone());
                }
            }
        }
        ffx_bail!("cannot find matching image artifact for names {:?}, type: {}", names, image_type)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    const IMAGE_JSON: &str = r#"[
        {
          "archive": true,
          "name": "buildargs",
          "path": "args.gn",
          "type": "gn"
        },
        {
          "archive": true,
          "name": "fastboot",
          "path": "host_x64/fastboot",
          "type": "exe.linux-x64"
        },
        {
          "name": "flash-script",
          "path": "flash.sh",
          "type": "script"
        },
        {
          "archive": true,
          "name": "flash-manifest",
          "path": "flash.json",
          "type": "manifest"
        },
        {
          "archive": true,
          "cpu": "x64",
          "label": "//zircon/kernel/target/pc/multiboot:multiboot(//zircon/kernel/target/pc/multiboot:zircon_multiboot)",
          "name": "qemu-kernel",
          "path": "multiboot.bin",
          "type": "kernel"
        },
        {
          "cpu": "x64",
          "label": "//zircon/kernel:kernel(//zircon/kernel:kernel_x64)",
          "name": "kernel",
          "path": "kernel_x64/kernel.zbi",
          "tags": [
            "incomplete"
          ],
          "type": "zbi"
        },
        {
          "archive": true,
          "bootserver_pave": [
            "--zirconr"
          ],
          "bootserver_pave_zedboot": [
            "--zircona"
          ],
          "name": "zircon-r",
          "path": "zedboot.zbi",
          "type": "zbi"
        },
        {
          "mkzedboot_mode": [
            "efi"
          ],
          "name": "zedboot-efi",
          "path": "zedboot.esp.blk",
          "type": "blk"
        },
        {
          "archive": true,
          "name": "bootserver",
          "path": "host_x64/bootserver",
          "type": "exe.linux-x64"
        },
        {
          "archive": true,
          "bootserver_pave": [
            "--boot",
            "--zircona"
          ],
          "fastboot_flash": [
          ],
          "name": "zircon-a",
          "path": "fuchsia.zbi",
          "type": "zbi"
        },
        {
          "archive": true,
          "bootserver_pave": [
            "--bootloader"
          ],
          "bootserver_pave_zedboot": [
            "--bootloader"
          ],
          "name": "efi",
          "path": "fuchsia.esp.blk",
          "type": "blk"
        },
        {
          "name": "blob",
          "path": "obj/build/images/blob.blk",
          "type": "blk"
        },
        {
          "name": "data",
          "path": "obj/build/images/data.blk",
          "type": "blk"
        },
        {
          "archive": true,
          "bootserver_pave": [
            "--fvm"
          ],
          "name": "storage-sparse",
          "path": "obj/build/images/fvm.sparse.blk",
          "type": "blk"
        },
        {
          "archive": true,
          "name": "storage-full",
          "path": "obj/build/images/fvm.blk",
          "type": "blk"
        },
        {
          "name": "zircon-vboot",
          "path": "fuchsia.zbi.signed",
          "type": "vboot"
        },
        {
          "name": "recovery-eng",
          "path": "obj/build/images/recovery/recovery-eng.zbi",
          "type": "zbi"
        },
        {
          "archive": false,
          "bootserver_netboot": [
            "--boot"
          ],
          "name": "netboot",
          "path": "netboot.zbi",
          "type": "zbi"
        },
        {
          "cpu": "x64",
          "label": "//zircon/kernel/phys/test:_qemu_phys_test.qemu-backtrace-test.executable(//zircon/kernel/arch/x86/phys:kernel.phys32)",
          "name": "_qemu_phys_test.qemu-backtrace-test.executable",
          "path": "kernel.phys32/_qemu_phys_test.qemu-backtrace-test.executable.bin",
          "type": "kernel"
        },
        {
          "compressed": true,
          "cpu": "x64",
          "label": "//zircon/kernel/phys/test:_qemu_phys_test.qemu-backtrace-test.zbi_test.zbi(//zircon/kernel/arch/x86/phys:kernel.phys32)",
          "name": "qemu-backtrace-test",
          "path": "kernel.phys32/obj/zircon/kernel/phys/test/qemu-backtrace-test.zbi",
          "tags": [
            "incomplete"
          ],
          "testonly": true,
          "type": "zbi"
        },
        {
          "cpu": "x64",
          "label": "//zircon/kernel/phys/test:zbi-backtrace-test.executable(//zircon/kernel/phys:kernel.phys_x64)",
          "name": "zbi-backtrace-test.executable",
          "path": "kernel.phys_x64/obj/zircon/kernel/phys/test/zbi-backtrace-test.executable.zbi",
          "type": "kernel"
        },
        {
          "compressed": true,
          "cpu": "x64",
          "label": "//zircon/kernel/phys/test:zbi-backtrace-test.zbi(//zircon/kernel/phys:kernel.phys_x64)",
          "name": "zbi-backtrace-test",
          "path": "kernel.phys_x64/obj/zircon/kernel/phys/test/zbi-backtrace-test.zbi",
          "testonly": true,
          "type": "zbi"
        },
        {
          "cpu": "x64",
          "label": "//zircon/kernel/phys/test:_qemu_phys_test.qemu-hello-world-test.executable(//zircon/kernel/arch/x86/phys:kernel.phys32)",
          "name": "_qemu_phys_test.qemu-hello-world-test.executable",
          "path": "kernel.phys32/_qemu_phys_test.qemu-hello-world-test.executable.bin",
          "type": "kernel"
        },
        {
          "compressed": true,
          "cpu": "x64",
          "label": "//zircon/kernel/phys/test:_qemu_phys_test.qemu-hello-world-test.zbi_test.zbi(//zircon/kernel/arch/x86/phys:kernel.phys32)",
          "name": "qemu-hello-world-test",
          "path": "kernel.phys32/obj/zircon/kernel/phys/test/qemu-hello-world-test.zbi",
          "tags": [
            "incomplete"
          ],
          "testonly": true,
          "type": "zbi"
        }
      ]"#;

    #[test]
    fn test_image_parse() -> Result<()> {
        let images = Images::from_string(IMAGE_JSON)?;
        assert_eq!(
            images.find_path(vec!["storage-full", "storage-sparse"], "blk")?,
            "obj/build/images/fvm.blk"
        );
        assert_eq!(
            images.find_path(vec!["storage-full", "storage-sparse", "foo"], "blk")?,
            "obj/build/images/fvm.blk"
        );
        assert_eq!(
            images.find_path(vec!["storage-sparse", "storage-full"], "blk")?,
            "obj/build/images/fvm.sparse.blk"
        );
        assert_eq!(images.find_path(vec!["qemu-kernel"], "kernel")?, "multiboot.bin");
        assert_eq!(images.find_path(vec!["zircon-a"], "zbi")?, "fuchsia.zbi");
        Ok(())
    }
}
