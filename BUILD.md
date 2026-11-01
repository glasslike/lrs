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
