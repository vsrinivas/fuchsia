//! Parition type constants
use log::trace;
use std::str::FromStr;

/// The type
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Type {
    /// Type-GUID for a GPT partition.
    pub guid: &'static str,
    /// well-known OS label for this type-GUID.
    pub os: OperatingSystem,
}

/// Operating System
#[derive(Debug, Clone, Eq, PartialEq, Hash)]
pub enum OperatingSystem {
    /// No OS
    None,
    /// Android OS
    Android,
    /// Atari
    Atari,
    /// Ceph
    Ceph,
    /// ChromeOS
    Chrome,
    /// CoreOs
    CoreOs,
    /// Custom
    Custom(String),
    /// FreeBsd
    FreeBsd,
    /// FreeDesktop
    FreeDesktop,
    /// Haiku
    Haiku,
    /// Hp Unix
    HpUnix,
    /// Linux
    Linux,
    /// MidnightBsd
    MidnightBsd,
    /// MacOs
    MacOs,
    /// NetBsd
    NetBsd,
    /// Onie
    Onie,
    /// OpenBSD
    OpenBsd,
    /// Plan9
    Plan9,
    /// PowerPC
    PowerPc,
    /// Solaris
    Solaris,
    /// VmWare
    VmWare,
    /// Windows
    Windows,
    /// QNX
    QNX,
}

impl FromStr for OperatingSystem {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "unused" => Ok(OperatingSystem::None),
            "android" => Ok(OperatingSystem::Android),
            "atari" => Ok(OperatingSystem::Atari),
            "Ceph" => Ok(OperatingSystem::Ceph),
            "Chrome" => Ok(OperatingSystem::Chrome),
            "FreeBsd" => Ok(OperatingSystem::FreeBsd),
            "FreeDesktop" => Ok(OperatingSystem::FreeDesktop),
            "Haiku" => Ok(OperatingSystem::Haiku),
            "HP-UX" => Ok(OperatingSystem::HpUnix),
            "Linux" => Ok(OperatingSystem::Linux),
            "MacOS" => Ok(OperatingSystem::MacOs),
            "MidnightBsd" => Ok(OperatingSystem::MidnightBsd),
            "Onie" => Ok(OperatingSystem::Onie),
            "PowerPc" => Ok(OperatingSystem::PowerPc),
            "Solaris Illumos" => Ok(OperatingSystem::Solaris),
            _ => Err(format!("Unknown operating system: {}", s)),
        }
    }
}

#[test]
fn test_partition_fromstr_guid() {
    let p = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
    let t = Type::from_str(p).unwrap();
    println!("result: {:?}", t);
    assert_eq!(t, LINUX_FS);
}

#[test]
fn test_partition_from_name() {
    // mix case as part of the test
    let p = "Linux_FS";
    let t = Type::from_name(p).unwrap();
    println!("result: {:?}", t);
    assert_eq!(t, LINUX_FS);
}

impl Type {
    /// Lookup a partition type by uuid
    pub fn from_uuid(u: &uuid::Uuid) -> Result<Self, String> {
        let uuid_str = u.to_hyphenated().to_string().to_uppercase();
        trace!("looking up partition type guid {}", uuid_str);
        Type::from_str(&uuid_str)
    }

    /// Lookup a partition type by name
    pub fn from_name(name: &str) -> Result<Self, String> {
        let name_str = name.to_uppercase();
        trace!("looking up partition type by name {}", name_str);
        Type::from_str(&name_str)
    }
}

impl Default for Type {
    fn default() -> Type {
        Type { guid: "00000000-0000-0000-0000-000000000000", os: OperatingSystem::None }
    }
}

partition_types! {
    /// unused
    (UNUSED, "00000000-0000-0000-0000-000000000000", OperatingSystem::None),
    /// MBR Partition Scheme
    (MBR, "024DEE41-33E7-11D3-9D69-0008C781F39F", OperatingSystem::None),
    /// EFI System Partition
    (EFI, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", OperatingSystem::None),
    /// BIOS Boot Partition
    (BIOS, "21686148-6449-6E6F-744E-656564454649", OperatingSystem::None),
    /// Intel Fast Flash (iFFS) Partition
    (FLASH, "D3BFE2DE-3DAF-11DF-BA40-E3A556D89593",OperatingSystem::None),
    /// Sony Boot Partition
    (SONY_BOOT, "F4019732-066E-4E12-8273-346C5641494F", OperatingSystem::None),
    /// Lenovo Boot Partition
    (LENOVO_BOOT, "BFBFAFE7-A34F-448A-9A5B-6213EB736C22", OperatingSystem::None),
    /// Microsoft Reserved Partition
    (MICROSOFT_RESERVED, "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", OperatingSystem::Windows),
    /// Basic Data Partition
    (BASIC, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", OperatingSystem::Windows),
    /// Logical Disk Manager Metadata Partition
    (WINDOWS_METADATA, "5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", OperatingSystem::Windows),
    /// Logical Disk Manager Data Partition
    (WINDOWS_DATA, "AF9B60A0-1431-4F62-BC68-3311714A69AD", OperatingSystem::Windows),
    /// Windows Recovery Environment
    (WINDOWS_RECOVERY, "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", OperatingSystem::Windows),
    /// IBM General Parallel File System Partition
    (WINDOWS_PARALLEL, "37AFFC90-EF7D-4E96-91C3-2D7AE055B174", OperatingSystem::Windows),
    /// Storage Spaces Partition
    (WINDOWS_STORAGESPACES, "E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", OperatingSystem::Windows),
    /// HP Unix Data Partition
    (HPUNIX_DATA, "75894C1E-3AEB-11D3-B7C1-7B03A0000000", OperatingSystem::HpUnix),
    /// HP Unix Service Partition
    (HPUNIX_SERVICE, "E2A1E728-32E3-11D6-A682-7B03A0000000", OperatingSystem::HpUnix),
    /// Linux Filesystem Data
    (LINUX_FS, "0FC63DAF-8483-4772-8E79-3D69D8477DE4", OperatingSystem::Linux),
    /// Linux RAID Partition
    (LINUX_RAID, "A19D880F-05FC-4D3B-A006-743F0F84911E", OperatingSystem::Linux),
    /// Linux Root Partition (x86)
    (LINUX_ROOT_X86, "44479540-F297-41B2-9AF7-D131D5F0458A", OperatingSystem::Linux),
    /// Linux Root Partition (x86-64)
    (LINUX_ROOT_X64, "4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", OperatingSystem::Linux),
    /// Linux Root Partition (32-bit ARM)
    (LINUX_ROOT_ARM_32, "69DAD710-2CE4-4E3C-B16C-21A1D49ABED3", OperatingSystem::Linux),
    /// Linux Root Partition (64-bit ARM/AArch64)
    (LINUX_ROOT_ARM_64, "B921B045-1DF0-41C3-AF44-4C6F280D3FAE", OperatingSystem::Linux),
    /// Linux Swap Partition
    (LINUX_SWAP, "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", OperatingSystem::Linux),
    /// Linux Logical Volume Manager Partition
    (LINUX_LVM, "E6D6D379-F507-44C2-A23C-238F2A3DF928", OperatingSystem::Linux),
    /// Linux /home Partition
    (LINUX_HOME, "933AC7E1-2EB4-4F13-B844-0E14E2AEF915", OperatingSystem::Linux),
    /// Linux /srv (Server Data) Partition
    (LINUX_SRV, "3B8F8425-20E0-4F3B-907F-1A25A76F98E8", OperatingSystem::Linux),
    /// Linux Plain dm-crypt Partition
    (LINUX_DMCRYPT, "7FFEC5C9-2D00-49B7-8941-3EA10A5586B7", OperatingSystem::Linux),
    /// Linux LUKS Partition
    (LINUX_LUKS, "CA7D7CCB-63ED-4C53-861C-1742536059CC", OperatingSystem::Linux),
    /// Linux Reserved
    (LINUX_RESERVED, "8DA63339-0007-60C0-C436-083AC8230908", OperatingSystem::Linux),
    /// FreeBSD Data Partition
    (FREEBSD_DATA, "516E7CB4-6ECF-11D6-8FF8-00022D09712B", OperatingSystem::FreeBsd),
    /// FreeBSD Boot Partition
    (FREEBSD_BOOT, "83BD6B9D-7F41-11DC-BE0B-001560B84F0F", OperatingSystem::FreeBsd),
    /// FreeBSD Swap Partition
    (FREEBSD_SWAP, "516E7CB5-6ECF-11D6-8FF8-00022D09712B", OperatingSystem::FreeBsd),
    /// FreeBSD Unix File System (UFS) Partition
    (FREEBSD_UFS, "516E7CB6-6ECF-11D6-8FF8-00022D09712B", OperatingSystem::FreeBsd),
    /// FreeBSD Vinium Volume Manager Partition
    (FREEBSD_VINIUM, "516E7CB8-6ECF-11D6-8FF8-00022D09712B", OperatingSystem::FreeBsd),
    /// FreeBSD ZFS Partition
    (FREEBSD_ZFS, "516E7CBA-6ECF-11D6-8FF8-00022D09712B", OperatingSystem::FreeBsd),
    /// Apple Hierarchical File System Plus (HFS+) Partition
    (MACOS_HFSPLUS, "48465300-0000-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple UFS
    (MACOS_UFS, "55465300-0000-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple ZFS
    (MACOS_ZFS, "6A898CC3-1DD2-11B2-99A6-080020736631", OperatingSystem::MacOs),
    /// Apple RAID Partition
    (MACOS_RAID, "52414944-0000-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// APple RAID Partition, offline
    (MACOS_RAID_OFFLINE, "52414944-5F4F-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple Boot Partition (Recovery HD)
    (MACOS_RECOVERY, "426F6F74-0000-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple Label
    (MACOS_LABEL, "4C616265-6C00-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple TV Recovery Partition
    (MACOS_TV_RECOVERY, "5265636F-7665-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple Core Storage Partition
    (MACOS_CORE, "53746F72-6167-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Apple SoftRAID_Status
    (MACOS_SOFTRAID_STATUS, "B6FA30DA-92D2-4A9A-96F1-871EC6486200", OperatingSystem::MacOs),
    /// Apple SoftRAID_Scratch
    (MACOS_SOFTRAID_SCRATCH, "2E313465-19B9-463F-8126-8A7993773801", OperatingSystem::MacOs),
    /// Apple SoftRAID_Volume
    (MACOS_SOFTRAID_VOLUME, "FA709C7E-65B1-4593-BFD5-E71D61DE9B02", OperatingSystem::MacOs),
    /// Apple SOftRAID_Cache
    (MACOS_SOFTRAID_CACHE, "BBBA6DF5-F46F-4A89-8F59-8765B2727503", OperatingSystem::MacOs),
    /// Apple APFS
    (MACOS_APFS, "7C3457EF-0000-11AA-AA11-00306543ECAC", OperatingSystem::MacOs),
    /// Solaris Boot Partition
    (SOLARIS_BOOT, "6A82CB45-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Root Partition
    (SOLARIS_ROOT, "6A85CF4D-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Swap Partition
    (SOLARIS_SWAP, "6A87C46F-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Backup Partition
    (SOLARIS_BACKUP, "6A8B642B-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris /var Partition
    (SOLARIS_VAR, "6A8EF2E9-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris /home Partition
    (SOLARIS_HOME, "6A90BA39-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Alternate Sector
    (SOLARIS_ALT, "6A9283A5-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Reserved
    (SOLARIS_RESERVED1, "6A945A3B-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Reserved
    (SOLARIS_RESERVED2, "6A9630D1-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Reserved
    (SOLARIS_RESERVED3, "6A980767-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Reserved
    (SOLARIS_RESERVED4, "6A96237F-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// Solaris Reserved
    (SOLARIS_RESERVED5, "6A8D2AC7-1DD2-11B2-99A6-080020736631", OperatingSystem::Solaris),
    /// NetBSD Swap Partition
    (NETBSD_SWAP, "49F48D32-B10E-11DC-B99B-0019D1879648", OperatingSystem::NetBsd),
    /// NetBSD FFS Partition
    (NETBSD_FFS, "49F48D5A-B10E-11DC-B99B-0019D1879648", OperatingSystem::NetBsd),
    /// NetBSD LFS Partition
    (NETBSD_LFS, "49F48D82-B10E-11DC-B99B-0019D1879648", OperatingSystem::NetBsd),
    /// NetBSD RAID Partition
    (NETBSD_RAID, "49F48DAA-B10E-11DC-B99B-0019D1879648", OperatingSystem::NetBsd),
    /// NetBSD Concatenated Partition
    (NETBSD_CONCAT, "2DB519C4-B10F-11DC-B99B-0019D1879648", OperatingSystem::NetBsd),
    /// NetBSD Encrypted Partition
    (NETBSD_ENCRYPTED, "2DB519EC-B10F-11DC-B99B-0019D1879648", OperatingSystem::NetBsd),
    /// ChromeOS Kernel
    (CHROME_KERNEL, "FE3A2A5D-4F32-41A7-B725-ACCC3285A309", OperatingSystem::Chrome),
    /// ChromeOS rootfs
    (CHROME_ROOTFS, "3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC",OperatingSystem::Chrome),
    /// ChromeOS Future Use
    (CHROME_FUTURE, "2E0A753D-9E48-43B0-8337-B15192CB1B5E",OperatingSystem::Chrome),
    /// CoreOS /usr partition (coreos-usr)
    (COREOS_USR, "5DFBF5F4-2848-4BAC-AA5E-0D9A20B745A6", OperatingSystem::CoreOs),
    /// CoreOS Resizable rootfs (coreos-resize)
    (COREOS_ROOTFS_RESIZE, "3884DD41-8582-4404-B9A8-E9B84F2DF50E", OperatingSystem::CoreOs),
    /// CoreOS OEM customizations (coreos-reserved)
    (COREOS_OEM, "C95DC21A-DF0E-4340-8D7B-26CBFA9A03E0", OperatingSystem::CoreOs),
    /// CoreOS Root filesystem on RAID (coreos-root-raid)
    (COREOS_ROOT_RAID, "BE9067B9-EA49-4F15-B4F6-F36F8C9E1818", OperatingSystem::CoreOs),
    /// Haiku BFS
    (HAIKU_BFS, "42465331-3BA3-10F1-802A-4861696B7521", OperatingSystem::Haiku),
    /// MidnightBSD Boot Partition
    (MIDNIGHT_BOOT, "85D5E45E-237C-11E1-B4B3-E89A8F7FC3A7", OperatingSystem::MidnightBsd),
    /// MidnightBSD Data Partition
    (MIDNIGHT_DATA, "85D5E45A-237C-11E1-B4B3-E89A8F7FC3A7", OperatingSystem::MidnightBsd),
    /// MidnightBSD Swap Partition
    (MIDNIGHT_SWAP, "85D5E45B-237C-11E1-B4B3-E89A8F7FC3A7", OperatingSystem::MidnightBsd),
    /// MidnightBSD Unix File System (UFS) Partition
    (MIDNIGHT_UFS, "0394EF8B-237E-11E1-B4B3-E89A8F7FC3A7", OperatingSystem::MidnightBsd),
    /// MidnightBSD Vinium Volume Manager Partition
    (MIDNIGHT_VINIUM, "85D5E45C-237C-11E1-B4B3-E89A8F7FC3A7", OperatingSystem::MidnightBsd),
    /// MidnightBSD ZFS Partition
    (MIDNIGHT_ZFS, "85D5E45D-237C-11E1-B4B3-E89A8F7FC3A7", OperatingSystem::MidnightBsd),
    /// Ceph Journal
    (CEPH_JOURNAL, "45B0969E-9B03-4F30-B4C6-B4B80CEFF106", OperatingSystem::Ceph),
    /// Ceph dm-crypt Encryted Journal
    (CEPH_CRYPT_JOURNAL, "45B0969E-9B03-4F30-B4C6-5EC00CEFF106", OperatingSystem::Ceph),
    /// Ceph OSD
    (CEPH_OSD, "4FBD7E29-9D25-41B8-AFD0-062C0CEFF05D", OperatingSystem::Ceph),
    /// Ceph dm-crypt OSD
    (CEPH_CRYPT, "4FBD7E29-9D25-41B8-AFD0-5EC00CEFF05D", OperatingSystem::Ceph),
    /// Ceph Disk In Creation
    (CEPH_DISK_CREATION, "89C57F98-2FE5-4DC0-89C1-F3AD0CEFF2BE", OperatingSystem::Ceph),
    /// Ceph dm-crypt Disk In Creation
    (CEPH_CRYPT_CREATION, "89C57F98-2FE5-4DC0-89C1-5EC00CEFF2BE", OperatingSystem::Ceph),
    /// OpenBSD Data Partition
    (OPENBSD_DATA, "824CC7A0-36A8-11E3-890A-952519AD3F61", OperatingSystem::OpenBsd),
    /// QNX Power-safe (QNX6) File System
    (QNX_FS, "CEF5A9AD-73BC-4601-89F3-CDEEEEE321A1", OperatingSystem::QNX),
    /// Plan 9 Partition
    (PLAN9_PART, "C91818F9-8025-47AF-89D2-F030D7000C2C", OperatingSystem::Plan9),
    /// VMWare vmkcore (coredump partition)
    (VMWARE_COREDUMP, "9D275380-40AD-11DB-BF97-000C2911D1B8", OperatingSystem::VmWare),
    /// VMWare VMFS Filesystem Partition
    (VMWARE_VMFS, "AA31E02A-400F-11DB-9590-000C2911D1B8", OperatingSystem::VmWare),
    /// VMware Reserved
    (VMWARE_RESERVED, "9198EFFC-31C0-11DB-8F78-000C2911D1B8", OperatingSystem::VmWare),
    /// Android Bootloader
    (ANDROID_BOOTLOADER, "2568845D-2332-4675-BC39-8FA5A4748D15", OperatingSystem::Android),
    /// Android Bootloader2
    (ANDROID_BOOTLOADER2, "114EAFFE-1552-4022-B26E-9B053604CF84", OperatingSystem::Android),
    /// Android Boot
    (ANDROID_BOOT, "49A4D17F-93A3-45C1-A0DE-F50B2EBE2599", OperatingSystem::Android),
    /// Android Recovery
    (ANDROID_RECOVERY, "4177C722-9E92-4AAB-8644-43502BFD5506", OperatingSystem::Android),
    /// Android Misc
    (ANDROID_MISC, "EF32A33B-A409-486C-9141-9FFB711F6266", OperatingSystem::Android),
    /// Android Metadata
    (ANDROID_META, "20AC26BE-20B7-11E3-84C5-6CFDB94711E9", OperatingSystem::Android),
    /// Android System
    (ANDROID_SYSTEM, "38F428E6-D326-425D-9140-6E0EA133647C", OperatingSystem::Android),
    /// Android Cache
    (ANDROID_CACHE, "A893EF21-E428-470A-9E55-0668FD91A2D9", OperatingSystem::Android),
    /// Android Data
    (ANDROID_DATA, "DC76DDA9-5AC1-491C-AF42-A82591580C0D", OperatingSystem::Android),
    /// Android Persistent
    (ANDROID_PERSISTENT, "EBC597D0-2053-4B15-8B64-E0AAC75F4DB1", OperatingSystem::Android),
    /// Android Factory
    (ANDROID_FACTORY, "8F68CC74-C5E5-48DA-BE91-A0C8C15E9C80", OperatingSystem::Android),
    /// Android Fastboot/Tertiary
    (ANDROID_FASTBOOT, "767941D0-2085-11E3-AD3B-6CFDB94711E9", OperatingSystem::Android),
    /// Android OEM
    (ANDROID_OEM, "AC6D7924-EB71-4DF8-B48D-E267B27148FF", OperatingSystem::Android),
    /// ONIE Boot
    (ONIE_BOOT, "7412F7D5-A156-4B13-81DC-867174929325", OperatingSystem::Onie),
    /// ONIE Config
    (ONIE_CONFIG, "D4E6E2CD-4469-46F3-B5CB-1BFF57AFC149", OperatingSystem::Onie),
    /// PowerPC PReP Boot
    (PPC_BOOT, "9E1A2D38-C612-4316-AA26-8B49521E5A8B", OperatingSystem::PowerPc),
    /// FreeDesktop Shared Boot Loader Configuration
    (FREEDESK_BOOT, "BC13C2FF-59E6-4262-A352-B275FD6F7172", OperatingSystem::FreeDesktop),
    /// Atari Basic Data Partition (GEM, BGM, F32)
    (ATARI_DATA, "734E5AFE-F61A-11E6-BC64-92361F002671", OperatingSystem::Atari),
}
