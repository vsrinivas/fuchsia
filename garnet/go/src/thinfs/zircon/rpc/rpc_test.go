package rpc

import (
	"syscall/zx"
	"testing"
	"thinfs/fs"
)

type dummyFs struct{}

func (d *dummyFs) Blockcount() int64           { return 0 }
func (d *dummyFs) Blocksize() int64            { return 0 }
func (d *dummyFs) Size() int64                 { return 0 }
func (d *dummyFs) Close() error                { return nil }
func (d *dummyFs) RootDirectory() fs.Directory { return nil }
func (d *dummyFs) Type() string                { return "dummy" }
func (d *dummyFs) FreeSize() int64             { return 0 }

func TestCookies(t *testing.T) {
	c1, c2, err := zx.NewChannel(0)
	if err != nil {
		t.Fatalf("failed to create channel: %v", err)
	}
	defer c1.Close()
	defer c2.Close()

	vfs, err := NewServer(&dummyFs{}, zx.Handle(c2))
	if err != nil {
		t.Fatalf("failed to create server: %v", err)
	}
	defer vfs.fs.Close()

	if len(vfs.dirs) != 1 {
		t.Fatalf("Unexpected number of directories. Want %d, got %d", 1, len(vfs.dirs))
	}

	for _, dir := range vfs.dirs {
		res, h, err := dir.GetToken(nil)
		if err != nil {
			t.Fatalf("GetToken(nil) failed: %v", err)
		}
		if zx.Status(res) != zx.ErrOk {
			t.Fatalf("GetToken(nil) returned wrong value. Want %v, got %v", zx.ErrOk, zx.Status(res))
		}

		dir.setCookie(32)
		if dir.getCookie(h) != 32 {
			t.Fatalf("Wrong Cookie retrieved. Want %d, got %d", 32, dir.getCookie(h))
		}
	}
}
