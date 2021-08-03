// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"image"
	"image/color/palette"
	"image/draw"
	"image/gif"
	"image/jpeg"
	"image/png"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	fg "go.fuchsia.dev/fuchsia/tools/femu-control/femu-grpc"
)

type recordScreenCmd struct {
	format      string
	numFrames   uint
	duration    time.Duration
	imageOutput string
	verbose     bool
}

func (*recordScreenCmd) Name() string     { return "record_screen" }
func (*recordScreenCmd) Synopsis() string { return "Record screen to a sequence of image files" }

func (*recordScreenCmd) Usage() string {
	return `record_screen: Record FEMU screen contents to a sequence of image files.

Usage:

record_screen [-num-frames <#>] [-duration <d>] [-v]
        [-out path/file%.jpg|file%.png|file.gif]

If neither -num-frames nor -duration arguments are present, the FEMU
screen will be recorded until ctrl-c (SIGINT) is received. In this case,
ctrl-c (SIGINT) is needed to stop recording and to write images to disk.

Output file can be jpg, png or gif format. For jpg/png files, filename
should contain a '%' symbol representing image number.

Flags:
`
}

func (c *recordScreenCmd) SetFlags(f *flag.FlagSet) {
	f.UintVar(&c.numFrames, "num-frames", 0, "maximum number of captured frames")
	f.DurationVar(&c.duration, "duration", time.Duration(0), "maximum recording time "+
		"(format should be acceptable to time.ParseDuration; e.g. 5.5s, 1m45s, 300ms)")
	f.StringVar(&c.imageOutput, "out", "./screenshot-%.png", "screenshot output path")
	f.BoolVar(&c.verbose, "v", false, "verbose mode (log screenshot filenames)")
}

func (c *recordScreenCmd) ValidateArgs() error {
	if c.imageOutput == "" {
		return fmt.Errorf("-out flag is required")
	}

	var err error = nil
	c.imageOutput, err = filepath.Abs(c.imageOutput)
	if err != nil {
		return fmt.Errorf("cannot get absolute path of '%s': %v", c.imageOutput, err)
	}
	dir, file := path.Split(c.imageOutput)
	fileInfo, err := os.Stat(dir)
	switch {
	case err != nil && !os.IsNotExist(err):
		return fmt.Errorf("path %s invalid: %v", dir, err)
	case err == nil && !fileInfo.IsDir():
		return fmt.Errorf("path %s is not a directory", dir)
	case err == nil && fileInfo.IsDir() && syscall.Access(dir, syscall.O_RDWR) != nil:
		return fmt.Errorf("path %s is not writeable", dir)
	}

	ext := filepath.Ext(file)
	basename := filepath.Base(file)

	if ext != ".gif" && strings.Count(basename, "%") != 1 {
		newFileName := basename[0:len(basename)-len(ext)] + "%" + ext
		c.imageOutput = path.Join(dir, newFileName)
		fmt.Fprintf(os.Stderr, "filename '%s' doesn't contain '%%', renamed to '%s'", file, newFileName)
	}

	switch ext {
	case ".jpeg", ".jpg", ".gif", ".png":
		break
	case "":
		return fmt.Errorf("filename '%s' doesn't contain any extension!", file)
	default:
		return fmt.Errorf("filename '%s' has unsupported extension: %s. "+
			"supported extensions are jpeg, jpg, gif, and png.", file, ext)
	}

	return nil
}

func genScreenshotFileName(name string, i int, max int) string {
	nameSubs := strings.Split(name, "%")
	prefix, suffix := nameSubs[0], nameSubs[1]

	maxDigits := len(strconv.Itoa(max))
	return fmt.Sprintf("%s%0*d%s", prefix, maxDigits, i, suffix)
}

func imageToPaletted(img image.Image) *image.Paletted {
	numColors := 256
	b := img.Bounds()

	pm := image.NewPaletted(b, palette.Plan9[:numColors])
	draw.FloydSteinberg.Draw(pm, b, img, b.Min)

	return pm
}

func (c *recordScreenCmd) run(ctx context.Context) error {
	dir, name := path.Split(c.imageOutput)
	nameSubs := strings.Split(name, ".")
	c.format = strings.ToLower(nameSubs[len(nameSubs)-1])

	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("error while creating directory %s: %v", dir, err)
	}

	client := ctx.Value("client").(fg.FemuGrpcClientInterface)
	if client == nil {
		return fmt.Errorf("FEMU gRPC client not found")
	}

	opts, err := fg.NewStreamScreenOpts("PNG", c.numFrames, c.duration)
	if err != nil {
		return fmt.Errorf("cannot create StreamScreenOpts: %v", err)
	}

	frames, err := client.StreamScreen(opts)
	if err != nil {
		return fmt.Errorf("error while FEMU gRPC streaming screen: %v", err)
	}

	var g *gif.GIF
	if c.format == "gif" {
		g = &gif.GIF{}
	}

	// Encode and store frames to images
	for i, b := range frames.Images {
		if c.format == "gif" {
			r := bytes.NewReader(b)
			img, err := png.Decode(r)
			if err != nil {
				return fmt.Errorf("error while decoding PNG: %v", err)
			}

			pm := imageToPaletted(img)

			g.Image = append(g.Image, pm)
			g.Delay = append(g.Delay, 10)

			continue
		}

		fn := genScreenshotFileName(name, i, len(frames.Images)-1)
		fpath := path.Join(dir, fn)

		if c.format == "jpg" || c.format == "jpeg" {
			r := bytes.NewReader(b)
			img, err := png.Decode(r)
			if err != nil {
				return fmt.Errorf("error while decoding PNG: %v", err)
			}

			f, err := os.Create(fpath)
			defer f.Close()
			if err != nil {
				return fmt.Errorf("error while creating file %s: %v", fpath, err)
			}

			err = jpeg.Encode(f, img, &jpeg.Options{Quality: 100})
			if err != nil {
				return fmt.Errorf("error while encoding and writing JPEG: %v", err)
			}

			if c.verbose {
				fmt.Println(fpath)
			}
			continue
		}

		if c.format == "png" {
			err := ioutil.WriteFile(fpath, b, 0644)
			if err != nil {
				return fmt.Errorf("error while writing file: %v", err)
			}

			if c.verbose {
				fmt.Println(fpath)
			}
			continue
		}
	}

	if c.format == "gif" {
		f, err := os.Create(c.imageOutput)
		defer f.Close()
		if err != nil {
			return fmt.Errorf("error while creating file %s: %v", c.imageOutput, err)
		}

		err = gif.EncodeAll(f, g)
		if err != nil {
			return fmt.Errorf("error while encoding and writing GIF: %v", err)
		}

		if c.verbose {
			fmt.Println(c.imageOutput)
		}
	}
	return nil
}

func (c *recordScreenCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := c.ValidateArgs(); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitUsageError
	}

	if err := c.run(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
