# Building lrs

`lrs` is a C11 program built with CMake. There are no extra libraries.
CMake 3.10 or newer is enough. The version string lives in
`CMakeLists.txt`. On a git checkout, configuring the build appends
`-g` and the short commit id when that string ends in `-dev`. Run
CMake again after a new commit if the binary should name that commit.
A tree without git keeps the plain version string.

The binary is `lrs` (or `lrs.exe` on Windows). The config file is not
installed. Pass it with `lrs -c /path/to/lrs.conf`.

## Debian

Debian 12 is the example. `build-essential` supplies the compiler and
make. CMake and git are separate packages.

```bash
sudo apt-get install -y build-essential cmake git

mkdir -p ~/src
cd ~/src
git clone https://github.com/glasslike/lrs.git
cd lrs

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

`lrs` is then `/usr/local/bin/lrs`. The prefix is chosen when configuring.
`cmake --install` uses that choice. Another directory is
`-DCMAKE_INSTALL_PREFIX` on the `cmake -S` line. `-DCMAKE_INSTALL_BINDIR=.`
puts the binary in the prefix itself instead of `prefix/bin`.

The `build` directory is only the compile tree. It is not part of the
source checkout you keep.

## Windows

You need CMake and a C compiler on `PATH`. Git is needed for the
`-dev` commit suffix, and to clone the repository. Two generators
are used below. Do not point both at the same `build` directory.
Delete that directory before switching generators.

### MinGW-w64

This is the generator used for the Windows binary built from this
tree. `gcc` must be the compiler CMake finds. A typical MinGW-w64
install provides it as `gcc` or `cc`.

```bat
git clone https://github.com/glasslike/lrs.git
cd lrs
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\lrs.exe --help
ctest --test-dir build --output-on-failure
```

`-DCMAKE_BUILD_TYPE=Release` is read only by single-configuration
generators such as MinGW Makefiles. Leave it off for Visual Studio.

### Visual Studio

From a shell where the Visual Studio compiler is already available
(the x64 Native Tools prompt, or a plain prompt after CMake's own
Visual Studio generator is installed):

```bat
git clone https://github.com/glasslike/lrs.git
cd lrs
cmake -S . -B build
cmake --build build --config Release
build\Release\lrs.exe --help
ctest --test-dir build -C Release --output-on-failure
```

CMake picks the installed Visual Studio as the default generator on
Windows when MinGW is not forced with `-G`.

### Install

```bat
cmake --install build --config Release
```

`--config Release` matters for the Visual Studio build. The MinGW
build can omit it. The default prefix is `C:/Program Files/lrs`, and
the program is `C:/Program Files/lrs/bin/lrs.exe`. Another directory
is `--prefix` on this command, or `-DCMAKE_INSTALL_PREFIX` when
configuring.

## Published binaries

GitHub Actions packs a binary drop. Nothing runs on a branch push.
Start one by hand (Actions, then the workflow, then Run workflow),
or push a tag. Every release workflow then builds that commit, and the
zips are attached to the release for the tag. Pushing the tag is enough:
the release is created if it does not exist yet.

Each zip holds one directory. Inside it: `lrs` (or `lrs.exe`),
`lrs.conf.example`, the two READMEs, `LICENSE`, `file_id.diz`, and
`requirements.txt`. The diz is the short BBS description. The
requirements file lists the shared libraries that binary was linked
against.

The name is `lrs-<version>-<system>-<arch>-<sha>.zip`. `<version>` is
`LRS_VERSION` from `CMakeLists.txt`. `<sha>` is seven hex digits of
the commit, with no `-g`.

| Workflow | Archive |
| --- | --- |
| Build on Debian 12 | `lrs-<version>-debian12-amd64-<sha>.zip`, and the same for `arm64` |
| Build on Debian 13 | `lrs-<version>-debian13-amd64-<sha>.zip`, and the same for `arm64` |
| Build on NetBSD 11 | `lrs-<version>-netbsd11-amd64-<sha>.zip` |
| Build on Windows | `lrs-<version>-win32-<sha>.zip` and `lrs-<version>-win64-<sha>.zip` |

The Windows drops are MinGW-w64 linked to `msvcrt.dll`, with libgcc
and winpthread inside the exe. That is the C library Windows XP already
has, and the same files load on later Windows. They are not the
Universal CRT, which a current MinGW on this machine links by default
and which XP does not ship. `win32` is the 32-bit exe, for 32-bit XP
and later. `win64` is the 64-bit exe, for XP x64 and later. The job
refuses the zip if the exe imports anything besides `kernel32.dll` and
`msvcrt.dll`, if one of those imports is a symbol XP does not export,
or if the header asks for a newer Windows than XP.

`Compile on Debian 12` only checks that the tree builds and the
self-test passes. It does not pack a zip.
