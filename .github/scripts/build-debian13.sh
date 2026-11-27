#!/bin/bash
# Build lrs inside a Debian 13 userspace and pack a binary drop.
#
# GitHub Actions checks the tree out on the Ubuntu runner, then runs this
# script with the tree mounted in debian:13 (see debian-13.yml). The
# self-test is a real run of the binary, so the compiler and the test
# must be the same Debian 13 system. Do not cross-compile.
#
# The architecture comes from the container, not from a CMake flag.
# linux/amd64 produces Debian "amd64". linux/arm64 (QEMU on an amd64
# runner, or a native arm64 host) produces Debian "arm64"; uname -m
# inside that container is aarch64. dpkg --print-architecture is what
# lands in the zip name and in file_id.diz.
#
# On success the script leaves one zip under dist/:
#   lrs-<version>-debian13-<arch>-<sha>.zip
# containing lrs, file_id.diz, requirements.txt, both READMEs, LICENSE,
# and lrs.conf.example. The zip is the Actions artifact.

set -euo pipefail

# One banner per phase. The whole script is a single Actions step, so
# without these the apt, configure, and compile logs run together.
step() {
	echo
	echo "---- $* ----"
}

export DEBIAN_FRONTEND=noninteractive

step "Install Debian 13 build packages"
apt-get update
apt-get install -y --no-install-recommends \
	build-essential cmake \
	git ca-certificates zip

# The runner owns the mount; git inside this container runs as root and
# otherwise refuses the checkout ("dubious ownership"). The -dev suffix
# in lrs --help comes from git during configure, so git must succeed.
# git itself is not in the debian:13 image, so this is after apt-get.
step "Trust the mounted git checkout"
git config --global --add safe.directory "$(pwd)"

step "Configure"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
step "Compile"
cmake --build build -j"$(nproc)"

step "Self-test"
ctest --test-dir build --output-on-failure

# set(LRS_VERSION "...") in the top CMakeLists.txt. The quotes are part
# of the CMake string, not of the version.
version="$(sed -n 's/^set(LRS_VERSION "\([^"]*\)").*/\1/p' CMakeLists.txt)"
if [ -z "$version" ]; then
	echo "could not read LRS_VERSION from CMakeLists.txt" >&2
	exit 1
fi

# Seven hex digits, the same abbreviation a -dev version uses after -g.
# The zip name has no g: lrs-0.1.1-dev-debian13-amd64-a9d086b.zip.
sha="$(git rev-parse --short=7 HEAD)"
when="$(TZ=UTC git log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M')"
arch="$(dpkg --print-architecture)"
name="lrs-${version}-debian13-${arch}-${sha}"

stage="dist/${name}"
rm -rf dist
mkdir -p "$stage"

# Stage a copy, then strip that copy. The build tree stays unstripped
# so a failed test is still debuggable in the Actions log.
step "Stage the stripped binary, docs, and the sample config"
install -m 755 build/lrs "$stage/lrs"
strip --strip-unneeded "$stage/lrs"
install -m 644 README.md README_RU.md LICENSE lrs.conf.example "$stage/"

# The dynamic libraries the binary was linked against, read from the ELF
# NEEDED entries rather than from the package list: this is what the
# target host actually has to provide. readelf ships with binutils, the
# same package as the strip above.
step "Write requirements.txt"
{
	printf 'Runtime libraries for this build (Debian 13, %s).\n' "$arch"
	printf 'Install them on the target host before running.\n'
	printf '\nlrs:\n'
	readelf -d "$stage/lrs" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/  \1/p'
} > "$stage/requirements.txt"

# The compiler that produced the binary above, as "gcc 14.2.0". The last
# line of -v is "gcc version 14.2.0 (Debian ...)" and names the real
# compiler; --version would say "cc" here, since cc is a symlink to gcc.
cc_bin="${CC:-cc}"
cc_ident="$("$cc_bin" -v 2>&1 | sed -n 's/^\(.*\) version \([0-9][^ ]*\).*/\1 \2/p' | tail -n 1)"
if [ -z "$cc_ident" ]; then
	cc_ident="unknown"
fi

# Classic BBS descriptor: ASCII, at most 10 lines, 45 columns.
# Written at pack time so the version and commit stay in step with the zip.
# Built here with LF so the width check below counts real columns; the
# file itself is converted to CRLF afterwards, as a DIZ is a DOS text file.
step "Write file_id.diz"
diz="$stage/file_id.diz"
diz_lf="$stage/.file_id.diz.lf"
{
	printf 'lrs %s, debian 13 (%s)\n' "$version" "$arch"
	printf '\n'
	printf 'lrs, lastread sync between client and server\n'
	printf 'ftn messagebases\n'
	printf '\n'
	printf 'Binary: lrs\n'
	printf 'Source %s %s UTC\n' "$sha" "$when"
	printf 'Compiler: %s\n' "$cc_ident"
} > "$diz_lf"

line_no=0
while IFS= read -r line || [ -n "$line" ]; do
	line_no=$((line_no + 1))
	# ${#line} is characters; the file is ASCII, so that is the column count.
	if [ "${#line}" -gt 45 ]; then
		echo "file_id.diz line $line_no is ${#line} columns (max 45): $line" >&2
		exit 1
	fi
done < "$diz_lf"
if [ "$line_no" -gt 10 ]; then
	echo "file_id.diz has $line_no lines (max 10)" >&2
	exit 1
fi

# awk, not sed: a "\r" in a sed replacement is not portable across seds.
awk '{ printf "%s\r\n", $0 }' "$diz_lf" > "$diz"
rm -f "$diz_lf"

# Paths inside the archive are relative to dist/, so unzip yields one directory.
step "Pack ${name}.zip"
( cd dist && zip -r -9 "${name}.zip" "$name" )

echo "packed dist/${name}.zip"
# The staged binary must actually start. --help prints the ident and
# exits 0. A missing library or a bad link fails the job here.
step "Run lrs --help"
"$stage/lrs" --help
