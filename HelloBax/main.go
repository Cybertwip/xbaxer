package main

/*
#cgo CXXFLAGS: -std=c++17
#include "hello.h"
*/
import "C"

import "fmt"

func main() {
	const a, b = 2, 3
	sum := int(C.hellobax_sum(C.int(a), C.int(b)))
	fmt.Printf("The cleng result of %d + %d is: %d\n", a, b, sum)
}
