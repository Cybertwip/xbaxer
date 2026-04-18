package main

/*
#cgo CXXFLAGS: -std=c++17
#include <stdlib.h>
#include "hello.h"
*/
import "C"

import (
	"fmt"
	"unsafe"
)

func main() {
	const cap = 256
	buf := (*C.char)(C.malloc(cap))
	defer C.free(unsafe.Pointer(buf))

	C.hellobax_greet(buf, C.int(cap))
	fmt.Println(C.GoString(buf))
}
