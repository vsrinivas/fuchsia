// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE filepackage examples

// +build !build_with_native_toolchain

package examples

import (
	"fmt"

	"fidl/fuchsia/examples"
)

// [START bits]
func ExampleFileMode() {
	fmt.Println(examples.FileModeRead.String())
	fmt.Println(examples.FileModeWrite | examples.FileModeExecute)

	// Output:
	// Read
	// Unknown
}

// [END bits]

// [START enums]
func ExampleLocationType() {
	fmt.Println(examples.LocationTypeMuseum.String())
	// Output: Museum
}

// [END enums]

// [START structs]
func ExampleColor() {
	red := examples.Color{Id: 1, Name: "ruby"}
	fmt.Println(red.Id)
	fmt.Println(red.Name)

	// Output:
	// 1
	// ruby
}

// [END structs]

// [START unions]
func ExampleJsonValue() {
	val := examples.JsonValueWithStringValue("hi")
	fmt.Println(val.Which() == examples.JsonValueStringValue)
	fmt.Println(val.StringValue)
	val.SetIntValue(1)
	fmt.Println(val.Which() == examples.JsonValueIntValue)
	fmt.Println(val.IntValue)

	// Output:
	// true
	// hi
	// true
	// 1
}

// [END unions]

// [START tables]
func ExampleUser() {
	var user examples.User
	fmt.Println(user.HasAge(), user.HasName())
	user.SetAge(30)
	user.SetName("John")
	fmt.Println(user.GetAge(), user.GetName())
	user.ClearAge()
	user.ClearName()
	fmt.Println(user.HasAge(), user.HasName())
	fmt.Println(user.GetNameWithDefault("Unknown"))

	// Output:
	// false false
	// 30 John
	// false false
	// Unknown
}

// [END tables]
