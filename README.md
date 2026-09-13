# xSysInfo

![XSysInfo](docs/xsysinfo.png)

`xSysInfo` is a comprehensive system information utility designed for AmigaOS. It provides detailed insights into your Amiga system's hardware and software configuration, along with benchmarking capabilities.

**Note:** This program does not contain any code from the original AmigaOS SysInfo tool.

## Features

*   **Detailed Hardware Information**: Get in-depth reports on your CPU, memory, drives (including SCSI), expansion boards, and cache.
*   **Software Environment Overview**: View details about your AmigaOS software setup.
*   **Benchmarking**: Includes Dhrystone benchmarks to assess your system's performance.
*   **Graphical User Interface (GUI)**: User-friendly interface for easy navigation and information display.
*   **Printing Support**: Print out system reports for documentation or sharing (For now, the output is saved to a file in RAM:)
*   **Localization**: Supports multiple languages for its interface.

## Building `xSysInfo`

To build `xSysInfo`, you will need a GCC cross-compiler for m68k-amigaos (e.g., `m68k-amigaos-gcc`). The build process also requires `make`, `curl`, `md5sum`, and `lha` to handle external dependencies.

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/reinauer/xsysinfo.git
    cd xsysinfo
    git submodule update --init
    ```

2.  **Build the project**:
    The Makefile will automatically handle fetching and building necessary third-party libraries (`flexcat` and `identify`).
    ```bash
    make
    ```

3.  **Create an Amiga Disk File (ADF)**:
    You can create a bootable `.adf` image containing `xSysInfo` and its necessary libraries using `xdftool`.
    ```bash
    make disk
    ```
    This will generate `xsysinfo-<version>.adf` in the project root.

## Running `xSysInfo`

The primary way to run `xSysInfo` on a real Amiga is via a GOTEK drive using the generated `xsysinfo-<version>.adf` disk image.

You can also run the ADF on an Amiga emulator (like WinUAE or FS-UAE).

Additionally, the `xsysinfo` binary itself can be executed directly from the shell on a real Amiga or emulator.
Run `xSysInfo BRIEF` from the shell to write a compact benchmark summary to the
CLI output. Run `xSysInfo FULL` to write the full report format to the CLI
output. Run `xSysInfo WHICH` for output compatible with the column-aligned
WhichAmiga style report format.

The System Software Installed tile opens on an overview of the OS, physical
ROM, active ROM, Workbench, SetPatch, and graphics system. Its cycle button
opens the library, device, resource, and MMU lists. Total Chip and Fast RAM
are shown in the hardware overview.

## Configuration

You can select whether xSysInfo is started in a window or on its own screen
by specifying a DISPLAY ToolType. DISPLAY=auto is the default and will select
window when your screen resolution is larger than 640x512, as xSysInfo is
assuming RTG mode. For lower resolutions it will start on a PAL or NTSC screen.
You can force either behavior with DISPLAY=window or DISPLAY=screen.

Dark mode uses a dark blue palette. Run `xSysInfo DARK` from the shell, or set
a DARK ToolType on the icon when starting from Workbench. Without DARK,
xSysInfo keeps the default palette.

![XSysInfo in windowed mode](docs/xsysinfo-windowed.png)


## Dependencies

`xSysInfo` uses the following third-party projects and data:

*   [FlexCat](https://github.com/adtools/flexcat): Builds the localization
    catalogs from the source submodule.
*   [Identify](http://identify.shredzone.org/): Identifies hardware and
    software. Developer headers come from the source submodule; the runtime
    library comes from
    [IdentifyUsr.lha](https://aminet.net/util/libs/IdentifyUsr.lha).
*   [IdentifyPci.lha](https://aminet.net/util/libs/IdentifyPci.lha): Supplies
    the `pci.db` identification database included on the disk image.
*   [openpci68k.lha](https://aminet.net/driver/other/openpci68k.lha): Supplies
    `openpci.library` for PCI device access.
*   [MMULib.lha](https://aminet.net/util/libs/MMULib.lha): Supplies
    `mmu.library` and the 68020/68030/68040/68060 support libraries included
    on the disk image.
*   [MuManual.lha](https://aminet.net/docs/misc/MuManual.lha): Supplies MMU
    developer headers and function descriptions used to generate compiler
    bindings.
*   [TinySetPatch](https://github.com/reinauer/TinySetPatch): Built from the
    source submodule for CPU support and system initialization when booting
    the disk image.
*   [fd2pragma](https://github.com/AmigaPorts/fd2pragma): Generates compiler
    bindings from library function descriptions and C prototypes. Must be
    preinstalled and available on `PATH`; the build does not install it.

The build downloads and caches the `.lha` archives in `downloads/`. It also
downloads `fd2pragma.types` to `~/.fd2pragma.types` when that file is missing.

## Contributing

We welcome contributions! Please feel free to fork the repository, make your changes, and submit a pull request.
