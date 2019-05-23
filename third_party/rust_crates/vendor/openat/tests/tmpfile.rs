extern crate tempfile;
extern crate openat;

use std::io::{self, Read, Write};
use std::os::unix::fs::PermissionsExt;
use openat::Dir;

#[test]
#[cfg(target_os="linux")]
fn unnamed_tmp_file_link() -> Result<(), io::Error> {
    let tmp = tempfile::tempdir()?;
    let dir = Dir::open(tmp.path())?;
    let mut f = dir.new_unnamed_file(0o777)?;
    f.write(b"hello\n")?;
    // In glibc <= 2.22 permissions aren't set when using O_TMPFILE
    // This includes ubuntu trusty on travis CI
    f.set_permissions(PermissionsExt::from_mode(0o644));
    dir.link_file_at(&f, "hello.txt")?;
    let mut f = dir.open_file("hello.txt")?;
    let mut buf = String::with_capacity(10);
    f.read_to_string(&mut buf)?;
    assert_eq!(buf, "hello\n");
    Ok(())
}
