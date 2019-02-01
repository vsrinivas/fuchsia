// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package merkle

import (
	"crypto/sha256"
	"encoding/binary"
	"hash"
	"io"
)

const (
	blockSize = 8192
	hashSize  = 32
)

func newHash() hash.Hash {
	return sha256.New()
}

// Tree is a representation of a portion of a Merkle Tree
type Tree struct {
	level  uint64
	hashes []byte
	offset uint64
	next   *Tree
}

func (t *Tree) addBlock(b []byte) {
	d := newHash()
	binary.Write(d, binary.LittleEndian, t.offset|t.level)
	binary.Write(d, binary.LittleEndian, uint32(len(b)))
	d.Write(b)
	if blockSize-len(b) != 0 {
		d.Write(make([]byte, blockSize-len(b)))
	}
	t.offset += uint64(len(b))
	t.hashes = append(t.hashes, d.Sum(nil)...)

	if len(t.hashes) > 0 && len(t.hashes)%blockSize == 0 {
		t.getNext().addBlock(t.hashes[len(t.hashes)-blockSize:])
	}
}

func (t *Tree) getNext() *Tree {
	if t.next == nil {
		t.next = &Tree{
			level: t.level + 1,
		}
	}
	return t.next
}

// finish pads out any remaining levels of the tree until the root digest is
// computed, returning the top of the tree.
func (t *Tree) finish() *Tree {
	if len(t.hashes) == 0 || len(t.hashes) == hashSize {
		return t
	}

	remainder := len(t.hashes) % blockSize
	if remainder != 0 {
		block := make([]byte, blockSize)
		copy(block, t.hashes[len(t.hashes)-remainder:])
		t.getNext().addBlock(block)
	}

	return t.getNext().finish()
}

// ReadFrom consumes r until EOF, computing tree digests along the way
func (t *Tree) ReadFrom(r io.Reader) (int64, error) {
	var total int64
	for {
		buf := make([]byte, blockSize)
		n, err := io.ReadFull(r, buf)
		total += int64(n)
		if n > 0 {
			t.addBlock(buf[:n])
		}

		switch err {
		case nil:
		case io.ErrUnexpectedEOF, io.EOF:
			return total, nil
		default:
			return total, err
		}
	}
}

// Root returns the digest at the top of the Merkle Tree
func (t *Tree) Root() []byte {
	t = t.finish()

	if len(t.hashes) == 0 {
		d := newHash()
		d.Write(make([]byte, 12))
		t.hashes = d.Sum(nil)
	}

	return t.hashes
}
