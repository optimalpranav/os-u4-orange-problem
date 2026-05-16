# PES1UG24CS336-pes-vcs

**Pranav SS | PES1UG24CS336 | <pranavshamnur@gmail.com>**

A mini version control system built from scratch in C, inspired by Git's internal design. Implements content-addressable object storage, a staging area, tree snapshots, and commit history — all stored as plain files under `.pes/`.

---

## Table of Contents

- [Installation](#installation)
- [Setup](#setup)
- [Build](#build)
- [Usage](#usage)
- [Running Tests](#running-tests)
- [Project Structure](#project-structure)
- [How It Works](#how-it-works)
- [Phase 5 & 6 — Analysis Questions](#phase-5--6--analysis-questions)

---

## Installation

### Prerequisites

Ubuntu 22.04 (or WSL on Windows). Install the required packages:

```
sudo apt update && sudo apt install -y gcc build-essential libssl-dev git
```

### Clone the Repository

```
git clone https://github.com/optimalpranav/PES1UG24CS336-pes-vcs
cd PES1UG24CS336-pes-vcs
```

### Set Your Author

```
export PES_AUTHOR="Pranav SS <pranavshamnur@gmail.com>"
```

To make it permanent:

```
echo 'export PES_AUTHOR="Pranav SS <pranavshamnur@gmail.com>"' >> ~/.bashrc
source ~/.bashrc
```

---

## Build

```
make          # Build the pes binary
make all      # Build pes + test binaries (test_objects, test_tree)
make clean    # Remove all compiled files
```

---

## Usage

### Initialize a repository

```
./pes init
```

Creates the `.pes/` directory structure with `objects/`, `refs/heads/`, and `HEAD`.

### Stage files

```
./pes add file.txt
./pes add file1.txt file2.txt src/main.c   # multiple files at once
```

Reads the file, hashes its contents, stores a blob object, and updates `.pes/index`.

### Check status

```
./pes status
```

Shows three sections:

- **Staged changes** — files added to the index since last commit
- **Unstaged changes** — tracked files modified or deleted since staging
- **Untracked files** — new files not yet staged

### Create a commit

```
./pes commit -m "Your commit message"
```

Builds a tree snapshot from the index, creates a commit object pointing to it, and moves the `main` branch pointer forward.

### View commit history

```
./pes log
```

Walks the commit chain from HEAD to the root, printing hash, author, date, and message for each commit.

### Full example workflow

```
./pes init
echo "hello world" > hello.txt
echo "goodbye" > bye.txt
./pes add hello.txt bye.txt
./pes status
./pes commit -m "Initial commit"

echo "more content" >> hello.txt
./pes add hello.txt
./pes commit -m "Update hello.txt"

./pes log
```

---

## Running Tests

### Phase 1 — Object Store

```
make test_objects
./test_objects
```

Tests blob storage, deduplication, and integrity checking.

### Phase 2 — Tree Objects

```
gcc -Wall -Wextra -O2 -o test_tree test_tree.c object.c tree.c index.c -lcrypto
./test_tree
```

Tests tree serialize/parse roundtrip and deterministic serialization.

### Phase 3 & 4 — Integration Test

```
make test-integration
```

Runs the full end-to-end test: init, add, commit three times, check log, verify object store and reference chain.

---

## Project Structure

```
PES1UG24CS336-pes-vcs/
├── pes.h           # Core types and constants (ObjectID, ObjectType, etc.)
├── object.c        # Content-addressable object store (Phase 1)
├── tree.h / tree.c # Tree object serialization and construction (Phase 2)
├── index.h / index.c # Staging area management (Phase 3)
├── commit.h / commit.c # Commit creation and history (Phase 4)
├── pes.c           # CLI entry point and command dispatch
├── test_objects.c  # Phase 1 tests
├── test_tree.c     # Phase 2 tests
├── test_sequence.sh # End-to-end integration test
└── Makefile
```

### `.pes/` directory layout after commits

```
.pes/
├── HEAD                        # "ref: refs/heads/main"
├── index                       # Staging area (text file)
├── objects/
│   ├── 2f/
│   │   └── 8a3b5c...           # Blob/tree/commit objects sharded by hash prefix
│   └── a1/
│       └── 9c4e6f...
└── refs/
    └── heads/
        └── main                # Current branch (contains latest commit hash)
```

---

## How It Works

### Object Storage (Phase 1)

Every piece of data is stored by its SHA-256 hash. The file format is:

```
<type> <size>\0<data>
```

For example a blob storing `"hello\n"`:

```
blob 6\0hello\n
```

The SHA-256 of this entire byte sequence becomes the filename, sharded into `.pes/objects/XX/YYY...` (first 2 hex chars = subdirectory). Identical files share one object — deduplication is automatic.

All writes use an atomic **temp file + rename** pattern so partial writes never corrupt the store.

### Tree Objects (Phase 2)

A tree represents a directory snapshot. Binary format per entry:

```
<mode-octal-ascii> <name>\0<32-byte-binary-hash>
```

Modes: `100644` (regular file), `100755` (executable), `040000` (directory/subtree).

Entries are **sorted by name** before serialization so identical directories always produce identical hashes regardless of insertion order.

### Index / Staging Area (Phase 3)

`.pes/index` is a human-readable text file:

```
100644 2cf8d83d9ee29543b34a8772... 1776751389 6 file1.txt
100644 e00c50e16a2df38f8d6bf809... 1776751389 6 file2.txt
```

Fields: `mode hash mtime size path`. The `mtime` and `size` fields allow fast change detection without re-hashing every file.

The `Index` struct (10,000 entries × ~560 bytes = 5.3 MB) is heap-allocated to avoid stack overflow.

### Commits (Phase 4)

Commit object text format:

```
tree <64-char-hex>
parent <64-char-hex>        ← omitted for first commit
author Pranav SS <pranav.ss@gmail.com> 1776751432
committer Pranav SS <pranav.ss@gmail.com> 1776751432

Your commit message here
```

`commit_create` flow:

1. `tree_from_index()` → build and store tree hierarchy from staged files
2. `head_read()` → get parent commit hash (skip for initial commit)
3. `commit_serialize()` → format the commit text
4. `object_write(OBJ_COMMIT, ...)` → store it
5. `head_update()` → atomically move branch pointer to new commit

---

## Phase 5 & 6 — Analysis Questions

### Phase 5 — Branching

**Q5.1: How would `pes checkout <branch>` work?**

Read `.pes/HEAD` to find the current branch, then overwrite it with `ref: refs/heads/<branch>`. Read the target branch file to get its commit hash, read that commit to get its tree hash, then recursively walk the tree and restore every file into the working directory. The complexity lies in handling files that exist in the current branch but not the target (must be deleted), and files that differ between branches.

**Q5.2: How to detect a dirty working directory before switching?**

For each entry in the index, call `lstat()` on the actual file and compare its `st_mtime` and `st_size` against the stored values in the index. If any differ, the file has been modified since staging — the working directory is dirty and checkout should be refused with an error message listing the conflicting files.

**Q5.3: What is detached HEAD and how to recover?**

Detached HEAD occurs when `.pes/HEAD` contains a raw commit hash instead of `ref: refs/heads/main`. This happens when you check out a specific commit directly. Any new commits made in this state are not reachable from any branch — they will be garbage collected eventually. To recover: create a new branch pointing to the current commit (`pes branch recovery-branch <hash>`), then `pes checkout recovery-branch`.

---

### Phase 6 — Garbage Collection

**Q6.1: Algorithm to find and delete unreachable objects**

```
1. reachable = empty set
2. For each file in .pes/refs/heads/:
     start = read commit hash from file
     queue = [start]
     while queue not empty:
         commit_hash = queue.pop()
         if commit_hash in reachable: continue
         reachable.add(commit_hash)
         commit = object_read(commit_hash)
         reachable.add(commit.tree)
         walk_tree(commit.tree, reachable)   # adds all blob/subtree hashes
         if commit.has_parent:
             queue.push(commit.parent)
3. For each file under .pes/objects/:
     hash = reconstruct from path
     if hash not in reachable:
         delete the file
```

Use a hash set for `reachable` — O(1) lookup. For 100,000 commits with an average of 10 objects each (blobs + trees), you'd visit roughly 1,000,000 objects in the traversal.

**Q6.2: Race condition between GC and a concurrent commit**

Timeline of the race:

1. GC scans all reachable objects — marks set S
2. Commit operation writes a new blob object B (not yet referenced by any commit)
3. GC scans `.pes/objects/` and deletes everything not in S — including B
4. Commit operation tries to write a tree referencing B — B is gone, repository is corrupted

Git avoids this by: (a) using a `.git/gc.lock` file so only one GC runs at a time, and (b) a **grace period** — objects newer than 2 weeks are never deleted even if unreachable, giving in-flight operations time to complete.

---

## Screenshots

### Phase 1

**1A — test_objects passing:** All 3 tests pass: blob storage, deduplication, integrity check.

**1B — Object store structure:**

```
find .pes/objects -type f
```

### Phase 2

**2A — test_tree passing:** Both tests pass: serialize/parse roundtrip, deterministic serialization.

**2B — Raw tree object (xxd):**

```
xxd .pes/objects/XX/YYY... | head -20
```

### Phase 3

**3A — pes status output:** Shows staged files after `./pes add file1.txt file2.txt`.

**3B — .pes/index contents:**

```
100644 2cf8d83d9ee29543b34a87727421fdecb7e3f3a183d337639025de576db9ebb4 1776751389 6 file1.txt
100644 e00c50e16a2df38f8d6bf809e181ad0248da6e6719f35f9f7e65d6f606199f7f 1776751389 6 file2.txt
```

### Phase 4

**4A — pes log output:** Shows full commit chain with hashes, author `Pranav SS <pranav.ss@gmail.com>`, timestamps and messages.

**4B — Object store after 3 commits:**

```
find .pes/objects -type f | wc -l   # 10 objects
```

**4C — Integration test:**

```
=== All integration tests completed ===
```
