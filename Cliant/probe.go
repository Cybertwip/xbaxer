package main

// Client side of the Xbox-firewall probe. Pairs with `sarver -probe`.
// See Sarver/probe.go for the protocol (4-byte magic "XBPR").

import (
	"fmt"
	"net"
	"net/url"
	"strings"
	"time"
)

var probeMagic = []byte("XBPR")

// probeTCPPorts and probeUDPPorts must match the lists in Sarver/probe.go.
// Source: https://learn.microsoft.com/gaming/gdk/docs/gdk-dev/console-dev/dev-kits/settings/configure-dev-network
var probeTCPPorts = []int{
	2303, 3076, 4016, 49152, 49157, 49160,
	4024, 4201, 4211, 4221, 4224, 4600, 4601, 8116, 8117, 9002, 9269,
}
var probeUDPPorts = []int{500, 3544, 4500}

const probeDialTimeout = 2 * time.Second
const probeReadTimeout = 2 * time.Second

type probeResult struct {
	Proto  string
	Port   int
	Status string // "open", "filtered", "refused", "error"
	Detail string
}

func runProbe(args []string) error {
	if len(args) == 0 {
		printUsage()
		return fmt.Errorf("missing xbox host")
	}
	host := strings.TrimSpace(args[0])
	if host == "" {
		return fmt.Errorf("xbox host must not be empty")
	}

	// Be lenient: accept full URLs (http://1.2.3.4, http://1.2.3.4:80/foo)
	// in addition to bare hosts. Anything else passed straight to net.Dial
	// would otherwise yield "lookup http://1.2.3.4: no such host".
	if u, err := url.Parse(host); err == nil && u.Host != "" {
		host = u.Hostname()
	}
	host = strings.TrimPrefix(host, "//")
	host = strings.TrimSuffix(host, "/")

	fmt.Printf("Probing Xbox firewall at %s ...\n", host)
	fmt.Printf("(make sure `sarver.exe -probe` is running on the console)\n\n")

	var results []probeResult
	for _, port := range probeTCPPorts {
		results = append(results, probeTCP(host, port))
	}
	for _, port := range probeUDPPorts {
		results = append(results, probeUDP(host, port))
	}

	fmt.Printf("%-8s %-6s %-10s %s\n", "PROTO", "PORT", "STATUS", "DETAIL")
	fmt.Printf("%-8s %-6s %-10s %s\n", "-----", "----", "------", "------")
	openTCP := []int{}
	openUDP := []int{}
	for _, r := range results {
		fmt.Printf("%-8s %-6d %-10s %s\n", r.Proto, r.Port, r.Status, r.Detail)
		if r.Status == "open" {
			if r.Proto == "tcp" {
				openTCP = append(openTCP, r.Port)
			} else {
				openUDP = append(openUDP, r.Port)
			}
		}
	}

	fmt.Println()
	if len(openTCP) > 0 {
		fmt.Printf("TCP ports that traverse the Xbox firewall: %v\n", openTCP)
		fmt.Printf("  → run `sarver.exe -listen 0.0.0.0:%d` and `cliant http://<xbox-ip>:%d health`\n",
			openTCP[0], openTCP[0])
	} else {
		fmt.Println("No TCP ports traverse the Xbox firewall in push mode.")
		fmt.Println("  → use reverse-poll: `cliant serve -listen 0.0.0.0:17777` on the host,")
		fmt.Println("    then `sarver.exe -reverse http://<host-ip>:17777` on the Xbox.")
	}
	if len(openUDP) > 0 {
		fmt.Printf("UDP ports that traverse the Xbox firewall: %v\n", openUDP)
		fmt.Printf("  → these can host a future UDP/QUIC transport.\n")
	}
	return nil
}

func probeTCP(host string, port int) probeResult {
	addr := net.JoinHostPort(host, fmt.Sprintf("%d", port))
	conn, err := net.DialTimeout("tcp", addr, probeDialTimeout)
	if err != nil {
		status := "filtered"
		msg := err.Error()
		switch {
		case strings.Contains(msg, "refused"):
			status = "refused"
		case strings.Contains(msg, "timeout"), strings.Contains(msg, "i/o timeout"):
			status = "filtered"
		}
		return probeResult{Proto: "tcp", Port: port, Status: status, Detail: trimErr(msg)}
	}
	defer conn.Close()

	_ = conn.SetDeadline(time.Now().Add(probeReadTimeout))
	if _, err := conn.Write(probeMagic); err != nil {
		return probeResult{Proto: "tcp", Port: port, Status: "error", Detail: "write: " + trimErr(err.Error())}
	}

	buf := make([]byte, 64)
	n, err := conn.Read(buf)
	if err != nil {
		return probeResult{Proto: "tcp", Port: port, Status: "open", Detail: "connected, no probe reply (port likely owned by another service)"}
	}
	reply := string(buf[:n])
	if !strings.HasPrefix(reply, string(probeMagic)) {
		return probeResult{Proto: "tcp", Port: port, Status: "open", Detail: fmt.Sprintf("connected, foreign reply: %q", strings.TrimSpace(reply))}
	}
	return probeResult{Proto: "tcp", Port: port, Status: "open", Detail: "echo OK (" + reply + ")"}
}

func probeUDP(host string, port int) probeResult {
	addr := net.JoinHostPort(host, fmt.Sprintf("%d", port))
	conn, err := net.DialTimeout("udp", addr, probeDialTimeout)
	if err != nil {
		return probeResult{Proto: "udp", Port: port, Status: "error", Detail: trimErr(err.Error())}
	}
	defer conn.Close()

	_ = conn.SetDeadline(time.Now().Add(probeReadTimeout))
	if _, err := conn.Write(probeMagic); err != nil {
		return probeResult{Proto: "udp", Port: port, Status: "error", Detail: "write: " + trimErr(err.Error())}
	}

	buf := make([]byte, 64)
	n, err := conn.Read(buf)
	if err != nil {
		return probeResult{Proto: "udp", Port: port, Status: "filtered", Detail: "no reply"}
	}
	reply := string(buf[:n])
	if !strings.HasPrefix(reply, string(probeMagic)) {
		return probeResult{Proto: "udp", Port: port, Status: "open", Detail: fmt.Sprintf("foreign reply: %q", strings.TrimSpace(reply))}
	}
	return probeResult{Proto: "udp", Port: port, Status: "open", Detail: "echo OK (" + reply + ")"}
}

func trimErr(s string) string {
	if i := strings.Index(s, ": "); i >= 0 && i < 80 {
		return s[i+2:]
	}
	if len(s) > 80 {
		return s[:77] + "..."
	}
	return s
}
