WINDOWS BUILD NOTES
====================

Some notes on how to build Trivechain Core for Windows.

The options known to work for building Trivechain Core on Windows are:

* On Linux, using the [Mingw-w64](https://mingw-w64.org/doku.php) cross compiler tool chain. Ubuntu Bionic 18.04 is required
and is the platform used to build the Trivechain Core Windows release binaries.
* On Windows, using [Windows
Subsystem for Linux (WSL)](https://msdn.microsoft.com/commandline/wsl/about) and the Mingw-w64 cross compiler tool chain.

Other options which may work, but which have not been extensively tested are (please contribute instructions):

* On Windows, using a POSIX compatibility layer application such as [cygwin](http://www.cygwin.com/) or [msys2](http://www.msys2.org/).
* On Windows, using a native compiler tool chain such as [Visual Studio](https://www.visualstudio.com).

Installing Windows Subsystem for Linux
---------------------------------------

With Windows 10, Microsoft has released a new feature named the [Windows
Subsystem for Linux (WSL)](https://msdn.microsoft.com/commandline/wsl/about). This
feature allows you to run a bash shell directly on Windows in an Ubuntu-based
environment. Within this environment you can cross compile for Windows without
the need for a separate Linux VM or server. Note that while WSL can be installed with
other Linux variants, such as OpenSUSE, the following instructions have only been
tested with Ubuntu.

This feature is not supported in versions of Windows prior to Windows 10 or on
Windows Server SKUs. In addition, it is available [only for 64-bit versions of
Windows](https://msdn.microsoft.com/en-us/commandline/wsl/install_guide).

Full instructions to install WSL are available on the above link.
To install WSL on Windows 10 with Fall Creators Update installed (version >= 16215.0) do the following:

1. Enable the Windows Subsystem for Linux feature
  * Open the Windows Features dialog (`OptionalFeatures.exe`)
  * Enable 'Windows Subsystem for Linux'
  * Click 'OK' and restart if necessary
2. Install Ubuntu
  * Open Microsoft Store and search for "Ubuntu 18.04" or use [this link](https://www.microsoft.com/store/productId/9N9TNGVNDL3Q)
  * Click Install
3. Complete Installation
  * Open a cmd prompt and type "Ubuntu1804"
  * Create a new UNIX user account (this is a separate account from your Windows account)

After the bash shell is active, you can follow the instructions below, starting
with the "Cross-compilation" section. Compiling the 64-bit version is
recommended, but it is possible to compile the 32-bit version.

Note for building with Windows Subsystem
------------------------------------------------------------

Note that for WSL the Trivechain Core source path MUST be somewhere in the default mount file system, for
example /usr/src/bitcoin, AND not under /mnt/d/. If this is not the case the dependency autoconf scripts will fail.
This means you cannot use a directory that is located directly on the host Windows file system to perform the build.

Fix weird problems with these codes below:

    PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
    find . -name \*|xargs dos2unix # This will make sure that all the files does not have Windows end of file
    sudo update-alternatives --config x86_64-w64-mingw32-g++ # Pick POSIX
    
Cross-compilation for Ubuntu and Windows Subsystem for Linux
------------------------------------------------------------

The steps below can be performed on Ubuntu (including in a VM) or WSL. The depends system
will also work on other Linux distributions, however the commands for
installing the toolchain will be different.

First, install the general dependencies:

    sudo apt update
    sudo apt upgrade
    sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git

A host toolchain (`build-essential`) is necessary because some dependency
packages (such as `protobuf`) need to build host utilities that are used in the
build process.

See [dependencies.md](dependencies.md) for a complete overview.

Next, install the toolchains:

    sudo apt-get install g++-mingw-w64-i686 mingw-w64-i686-dev g++-mingw-w64-x86-64 mingw-w64-x86-64-dev

To build executables for Windows 32-bit:

    cd depends
    make HOST=i686-w64-mingw32 -j4
    cd ..
    ./configure --prefix=`pwd`/depends/i686-w64-mingw32
    make

To build executables for Windows 64-bit:

    cd depends
    make HOST=x86_64-w64-mingw32 -j4
    cd ..
    ./configure --prefix=`pwd`/depends/x86_64-w64-mingw32
    sudo update-alternatives --config x86_64-w64-mingw32-g++ # Pick POSIX
    make
