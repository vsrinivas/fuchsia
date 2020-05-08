// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package inspect

import (
	"fmt"
	"io"
)

const sizeBase = 16
const headerLen = 8

func writeUint16(w io.Writer, v uint16) error {
	var b [2]byte
	for i := range b {
		b[i] = byte(v >> (i * 8))
	}
	_, err := w.Write(b[:])
	return err
}

func writeUint24(w io.Writer, v uint32) error {
	var b [3]byte
	for i := range b {
		b[i] = byte(v >> (i * 8))
	}
	_, err := w.Write(b[:])
	return err
}

func writeUint32(w io.Writer, v uint32) error {
	var b [4]byte
	for i := range b {
		b[i] = byte(v >> (i * 8))
	}
	_, err := w.Write(b[:])
	return err
}

func writePadding(w io.Writer, width int) error {
	_, err := w.Write(make([]byte, width))
	return err
}

// Writer implements low-level streaming serialization in the inspect VMO
// format following the specification at
// https://fuchsia.dev/fuchsia-src/concepts/components/inspect/vmo_format.
type Writer struct {
	writer    io.Writer
	nextIndex uint32
}

func NewWriter(writer io.Writer) (*Writer, error) {
	w := Writer{
		writer: writer,
	}
	return &w, w.writeHeaderBlock()
}

func (w *Writer) writeHeader(order uint8, typ uint8) error {
	if _, err := w.writer.Write([]byte{order, typ}); err != nil {
		return err
	}
	w.nextIndex += 1 << order
	return nil
}

func (w *Writer) writeHeaderBlock() error {
	// Header.
	if err := w.writeHeader(0, 2); err != nil {
		return err
	}
	// Version.
	if err := writeUint16(w.writer, 1); err != nil {
		return err
	}
	if _, err := io.WriteString(w.writer, "INSP"); err != nil {
		return err
	}
	// Generation count.
	if err := writePadding(w.writer, 8); err != nil {
		return err
	}
	return nil
}

func (w *Writer) writeNameBlock(name string) (uint32, error) {
	const maxLength = 1<<13 - 1
	if len(name) > maxLength {
		return 0, fmt.Errorf("name too long: %d/%d bytes", len(name), maxLength)
	}
	index := w.nextIndex
	var order uint8
	for len(name) > sizeBase<<order-headerLen {
		order++
	}
	// Header.
	if err := w.writeHeader(order, 9); err != nil {
		return 0, err
	}
	// Length. NB: length is 12 bits, but is followed by 36 reserved bits.
	if err := writeUint16(w.writer, uint16(len(name))); err != nil {
		return 0, err
	}
	// Reserved.
	if err := writePadding(w.writer, 4); err != nil {
		return 0, err
	}
	// Payload.
	n, err := io.WriteString(w.writer, name)
	if err != nil {
		return 0, err
	}
	// Padding.
	if err := writePadding(w.writer, sizeBase<<order-headerLen-n); err != nil {
		return 0, err
	}
	return index, nil
}

func (w *Writer) WriteNodeValueBlock(parentIndex uint32, name string) (uint32, error) {
	nodeValueNameIndex, err := w.writeNameBlock(name)
	if err != nil {
		return 0, err
	}
	nodeValueIndex := w.nextIndex
	// NodeValue.
	if err := w.writeHeader(0, 3); err != nil {
		return 0, err
	}
	// Parent index.
	if err := writeUint24(w.writer, parentIndex); err != nil {
		return 0, err
	}
	// Name index.
	if err := writeUint24(w.writer, nodeValueNameIndex); err != nil {
		return 0, err
	}
	// Reference count.
	if err := writePadding(w.writer, 8); err != nil {
		return 0, err
	}
	return nodeValueIndex, nil
}

func (w *Writer) WriteBinary(parentIndex uint32, name string, size uint32, r io.Reader) error {
	bufferValueNameIndex, err := w.writeNameBlock(name)
	if err != nil {
		return err
	}

	// BufferValue.
	if err := w.writeHeader(0, 7); err != nil {
		return err
	}
	// Parent index.
	if err := writeUint24(w.writer, parentIndex); err != nil {
		return err
	}
	// Name index.
	if err := writeUint24(w.writer, bufferValueNameIndex); err != nil {
		return err
	}
	// Total length.
	if err := writeUint32(w.writer, size); err != nil {
		return err
	}
	// Extent index.
	if err := writeUint24(w.writer, w.nextIndex); err != nil {
		return err
	}
	// Display format = binary.
	if _, err := w.writer.Write([]byte{1 << 4}); err != nil {
		return err
	}
	// Extent.
	for {
		var order uint8
		for order < 7 && size > sizeBase<<order-headerLen {
			order++
		}
		if err := w.writeHeader(order, 8); err != nil {
			return err
		}
		// Next extent index.
		if err := writeUint24(w.writer, w.nextIndex); err != nil {
			return err
		}
		// Reserved.
		if err := writePadding(w.writer, 3); err != nil {
			return err
		}
		n, err := io.CopyN(w.writer, r, sizeBase<<order-headerLen)
		if err == io.EOF {
			if err := writePadding(w.writer, sizeBase<<order-headerLen-int(n)); err != nil {
				return err
			}
			break
		} else if err != nil {
			return err
		}
	}
	return nil
}
