package main

import (
	"flag"
	"fmt"
	"os"
	"sort"

	git "github.com/go-git/go-git/v5"
	"github.com/go-git/go-git/v5/config"
	"github.com/go-git/go-git/v5/plumbing"
	"github.com/go-git/go-git/v5/storage/memory"
)

func main() {
	repoURL := flag.String("repo", "https://github.com/go-git/go-git", "repository URL to inspect or clone")
	outputDir := flag.String("out", "", "clone target directory; leave empty to list remote refs instead")
	branch := flag.String("branch", "", "branch name to clone")
	depth := flag.Int("depth", 1, "clone depth")
	flag.Parse()

	if *outputDir == "" {
		if err := listRemoteRefs(*repoURL); err != nil {
			fmt.Fprintf(os.Stderr, "dastrabution: %v\n", err)
			os.Exit(1)
		}
		return
	}

	if err := cloneRepo(*repoURL, *outputDir, *branch, *depth); err != nil {
		fmt.Fprintf(os.Stderr, "dastrabution: %v\n", err)
		os.Exit(1)
	}
}

func listRemoteRefs(repoURL string) error {
	remote := git.NewRemote(memory.NewStorage(), &config.RemoteConfig{
		Name: "origin",
		URLs: []string{repoURL},
	})

	refs, err := remote.List(&git.ListOptions{})
	if err != nil {
		return fmt.Errorf("list refs for %s: %w", repoURL, err)
	}

	sort.Slice(refs, func(i, j int) bool {
		return refs[i].Name().String() < refs[j].Name().String()
	})

	for _, ref := range refs {
		if ref.Type() != plumbing.HashReference {
			continue
		}
		fmt.Printf("%s %s\n", ref.Hash(), ref.Name())
	}

	return nil
}

func cloneRepo(repoURL, outputDir, branch string, depth int) error {
	cloneOptions := &git.CloneOptions{
		URL:      repoURL,
		Depth:    max(depth, 1),
		Progress: os.Stdout,
	}
	if branch != "" {
		cloneOptions.ReferenceName = plumbing.NewBranchReferenceName(branch)
		cloneOptions.SingleBranch = true
	}

	if _, err := git.PlainClone(outputDir, false, cloneOptions); err != nil {
		return fmt.Errorf("clone %s into %s: %w", repoURL, outputDir, err)
	}

	fmt.Printf("cloned %s into %s\n", repoURL, outputDir)
	return nil
}
