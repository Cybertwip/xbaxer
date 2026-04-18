// Copyright 2023 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package ld

import (
	"cmp"
	"cmd/internal/sys"
	"cmd/link/internal/loader"
	"cmd/link/internal/sym"
	"slices"
)

// sehEntry holds the data needed to emit a single .pdata RUNTIME_FUNCTION
// entry. Entries are collected during writeSEH (before addresses are assigned)
// and emitted sorted by function virtual address in finalizeSEH (after
// textaddress has assigned virtual addresses to all text symbols).
type sehEntry struct {
	sym      loader.Sym // the function symbol
	size     int64      // function size (EndAddress = sym + size)
	xdataOff int64      // offset of the unwind info in the .xdata section
}

var sehp struct {
	pdata    []sym.LoaderSym
	xdata    []sym.LoaderSym
	entries  []sehEntry // collected during writeSEH, consumed by finalizeSEH
	xdataSym loader.Sym // .xdata section symbol, used by finalizeSEH
}

func writeSEH(ctxt *Link) {
	switch ctxt.Arch.Family {
	case sys.AMD64:
		writeSEHAMD64(ctxt)
	}
}

// finalizeSEH must be called after textaddress() has assigned addresses to
// text symbols. It sorts the collected .pdata entries by function virtual
// address and emits the .pdata section so that entries are ordered by
// BeginAddress as required by the PE specification and expected by strict
// linkers such as the Xbox GXDK linker.
func finalizeSEH(ctxt *Link) {
	switch ctxt.Arch.Family {
	case sys.AMD64:
		finalizeSEHAMD64(ctxt)
	}
}

func writeSEHAMD64(ctxt *Link) {
	ldr := ctxt.loader
	xdata := ldr.CreateSymForUpdate(".xdata", 0)
	xdata.SetType(sym.SSEHSECT)
	xdata.SetAlign(4)
	// The .xdata entries have very low cardinality
	// as it only contains frame pointer operations,
	// which are very similar across functions.
	// These are referenced by .pdata entries using
	// an RVA, so it is possible, and binary-size wise,
	// to deduplicate .xdata entries.
	uwcache := make(map[string]int64) // aux symbol name --> .xdata offset
	for _, s := range ctxt.Textp {
		if fi := ldr.FuncInfo(s); !fi.Valid() {
			continue
		}
		uw := ldr.SEHUnwindSym(s)
		if uw == 0 {
			continue
		}
		name := ctxt.SymName(uw)
		off, cached := uwcache[name]
		if !cached {
			off = xdata.Size()
			uwcache[name] = off
			xdata.AddBytes(ldr.Data(uw))
			// The SEH unwind data can contain relocations,
			// make sure those are copied over.
			rels := ldr.Relocs(uw)
			for i := 0; i < rels.Count(); i++ {
				r := rels.At(i)
				rel, _ := xdata.AddRel(r.Type())
				rel.SetOff(int32(off) + r.Off())
				rel.SetSiz(r.Siz())
				rel.SetSym(r.Sym())
				rel.SetAdd(r.Add())
			}
		}

		// Collect entry for deferred .pdata emission in finalizeSEH.
		sehp.entries = append(sehp.entries, sehEntry{
			sym:      s,
			size:     ldr.SymSize(s),
			xdataOff: off,
		})
	}
	sehp.xdataSym = xdata.Sym()
	sehp.xdata = append(sehp.xdata, xdata.Sym())
}

func finalizeSEHAMD64(ctxt *Link) {
	if len(sehp.entries) == 0 {
		return
	}
	ldr := ctxt.loader

	// Sort .pdata entries by function virtual address so the resulting
	// .pdata section is ordered by BeginAddress RVA.
	slices.SortStableFunc(sehp.entries, func(a, b sehEntry) int {
		return cmp.Compare(ldr.SymValue(a.sym), ldr.SymValue(b.sym))
	})

	pdata := ldr.CreateSymForUpdate(".pdata", 0)
	pdata.SetType(sym.SSEHSECT)
	pdata.SetAlign(4)

	for _, e := range sehp.entries {
		// Reference:
		// https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64#struct-runtime_function
		pdata.AddPEImageRelativeAddrPlus(ctxt.Arch, e.sym, 0)
		pdata.AddPEImageRelativeAddrPlus(ctxt.Arch, e.sym, e.size)
		pdata.AddPEImageRelativeAddrPlus(ctxt.Arch, sehp.xdataSym, e.xdataOff)
	}
	sehp.pdata = append(sehp.pdata, pdata.Sym())
}
