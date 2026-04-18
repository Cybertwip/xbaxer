package main

// Empirical Xbox-devkit firewall probe.
//
// The on-device firewall ships with an allowlist of inbound ports
// (e.g. TCP 53/80/3074, UDP 53/88/500/3074/3544/4500), but we have no
// reliable documentation of which entries are actually free for our use
// vs. reserved by the devkit's own services. `sarver -probe` opens a
// listener on every candidate port and logs anything that arrives, so the
// matching `cliant probe` subcommand can produce a definitive table of
// which ports the firewall lets through end-to-end.
//
// This file is intentionally self-contained so the probe can be extended
// without touching the production build/relay paths in main.go.

import (
	"fmt"
	"log"
	"net"
	"strings"
	"sync"
	"time"
)

// probeMagic is the 4-byte tag both sides exchange so we don't confuse a
// real client opening port 80 by accident with a probe response.
var probeMagic = []byte("XBPR")

// probeTCPPorts is the set of TCP ports we open listeners on. These are the
// ports Microsoft documents as "Required ports to enable communication with
// the development PC" in the GDK "configure-dev-network" guide:
//
//	https://learn.microsoft.com/gaming/gdk/docs/gdk-dev/console-dev/dev-kits/settings/configure-dev-network
//
// We omit 11442/11443 (already bound by Windows Device Portal on the devkit)
// and 22 (already bound by SSH). The remaining six are explicitly listed as
// dev-PC traffic and should pass the on-device firewall in both directions.
// First tier: "Required ports to enable communication with the development PC".
// Second tier: Xbox-One-footnote dev ports (VS2019/Xbox Manager/PIX/TAK/Game
// Streaming/VS Integration) — these are what the IDE actually uses to push
// builds, so at least one of them has to be open on every live devkit.
var probeTCPPorts = []int{
	2303, 3076, 4016, 49152, 49157, 49160,
	4024, 4201, 4211, 4221, 4224, 4600, 4601, 8116, 8117, 9002, 9269,
}

// probeUDPPorts mirrors the documented multiplayer/Teredo allowlist; useful
// only as a future fallback for a UDP/QUIC transport (HTTP doesn't use UDP).
var probeUDPPorts = []int{500, 3544, 4500}

func runProbe() error {
	log.Printf("sarver probe: opening listeners on Xbox-firewall-allowlisted ports")
	log.Printf("sarver probe: TCP=%v UDP=%v", probeTCPPorts, probeUDPPorts)

	var wg sync.WaitGroup
	var failed []string
	var failedMu sync.Mutex
	noteFailure := func(label string, err error) {
		failedMu.Lock()
		defer failedMu.Unlock()
		failed = append(failed, fmt.Sprintf("%s: %v", label, err))
	}

	for _, port := range probeTCPPorts {
		port := port
		listener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", port))
		if err != nil {
			noteFailure(fmt.Sprintf("tcp/%d listen", port), err)
			continue
		}
		log.Printf("sarver probe: TCP listening on :%d", port)
		wg.Add(1)
		go func() {
			defer wg.Done()
			defer listener.Close()
			for {
				conn, err := listener.Accept()
				if err != nil {
					log.Printf("sarver probe: tcp/%d accept error: %v", port, err)
					return
				}
				go handleProbeTCP(port, conn)
			}
		}()
	}

	for _, port := range probeUDPPorts {
		port := port
		conn, err := net.ListenPacket("udp", fmt.Sprintf("0.0.0.0:%d", port))
		if err != nil {
			noteFailure(fmt.Sprintf("udp/%d listen", port), err)
			continue
		}
		log.Printf("sarver probe: UDP listening on :%d", port)
		wg.Add(1)
		go func() {
			defer wg.Done()
			defer conn.Close()
			handleProbeUDP(port, conn)
		}()
	}

	if len(failed) > 0 {
		log.Printf("sarver probe: some listeners failed (likely already bound by another devkit service):")
		for _, line := range failed {
			log.Printf("  - %s", line)
		}
	}

	log.Printf("sarver probe: ready. Run `cliant probe <xbox-ip>` from your host to test connectivity.")
	wg.Wait()
	return nil
}

// handleProbeTCP reads up to len(probeMagic)+8 bytes, validates the magic,
// and echoes back the magic plus the listening port. Anything else is
// logged but treated as a successful TCP reach (we still saw inbound).
func handleProbeTCP(port int, conn net.Conn) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))

	buf := make([]byte, len(probeMagic))
	n, err := conn.Read(buf)
	remote := conn.RemoteAddr().String()
	if err != nil || n < len(probeMagic) {
		log.Printf("sarver probe: tcp/%d inbound from %s (handshake failed: %v)", port, remote, err)
		return
	}
	if string(buf) != string(probeMagic) {
		log.Printf("sarver probe: tcp/%d inbound from %s (non-probe payload: %q)", port, remote, strings.TrimSpace(string(buf)))
		return
	}

	resp := append([]byte{}, probeMagic...)
	resp = append(resp, fmt.Sprintf("|tcp|%d", port)...)
	if _, err := conn.Write(resp); err != nil {
		log.Printf("sarver probe: tcp/%d reply to %s failed: %v", port, remote, err)
		return
	}
	log.Printf("sarver probe: tcp/%d OK from %s", port, remote)
}

// handleProbeUDP echoes magic+|udp|<port> back to whoever sent us magic.
func handleProbeUDP(port int, conn net.PacketConn) {
	buf := make([]byte, 1024)
	for {
		_ = conn.SetReadDeadline(time.Time{}) // block indefinitely
		n, addr, err := conn.ReadFrom(buf)
		if err != nil {
			log.Printf("sarver probe: udp/%d read error: %v", port, err)
			return
		}
		if n < len(probeMagic) || string(buf[:len(probeMagic)]) != string(probeMagic) {
			log.Printf("sarver probe: udp/%d non-probe datagram from %s (%d bytes)", port, addr, n)
			continue
		}
		resp := append([]byte{}, probeMagic...)
		resp = append(resp, fmt.Sprintf("|udp|%d", port)...)
		if _, err := conn.WriteTo(resp, addr); err != nil {
			log.Printf("sarver probe: udp/%d reply to %s failed: %v", port, addr, err)
			continue
		}
		log.Printf("sarver probe: udp/%d OK from %s", port, addr)
	}
}
