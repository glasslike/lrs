#!/bin/bash
# Compile lrs on Debian 12 and stop there. No stripping, no staging,
# no zip: this is the gate that answers one question, does the tree
# still build, and it has to answer it fast.
#
# The release drop is built by build-debian12.sh. The compiler and the
# CMake flags here match a native Release build, so a break caught by
# this check is a break in the drop too. Anything about packaging
# belongs in that script, not here.
#
# Run by .github/workflows/compile-check.yml inside debian:12 on the
# runner's own architecture.

set -euo pipefail

# One banner per phase. The whole script is a single Actions step, so
# without these the apt, configure, and compile logs run together.
step() {
	echo
	echo "---- $* ----"
}

export DEBIAN_FRONTEND=noninteractive

step "Install Debian 12 build packages"
apt-get update
apt-get install -y --no-install-recommends \
	build-essential cmake \
	git ca-certificates

# The runner owns the mount; git inside this container runs as root and
# otherwise refuses the checkout ("dubious ownership"). The -dev suffix
# in lrs --help comes from git during configure, so git must succeed.
# git itself is not in the debian:12 image, so this is after apt-get.
step "Trust the mounted git checkout"
git config --global --add safe.directory "$(pwd)"

step "Configure"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
step "Compile"
cmake --build build -j"$(nproc)"

step "Self-test"
ctest --test-dir build --output-on-failure

step "Run lrs --help"
./build/lrs --help
