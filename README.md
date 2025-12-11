# xSysInfo

![XSysInfo](docs/xsysinfo.png)

`xSysInfo` is a comprehensive system information utility designed for AmigaOS. It provides detailed insights into your Amiga system's hardware and software configuration, along with benchmarking capabilities.

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
    This will generate `xsysinfo.adf` in the project root.

## Running `xSysInfo`

After building, you can run `xSysInfo` on an Amiga emulator (like WinUAE or FS-UAE) by loading the generated `xsysinfo.adf` disk image. Alternatively, if building directly on an Amiga machine, you can execute the `xsysinfo` binary from your shell.

## Dependencies

`xSysInfo` utilizes the following third-party projects:
*   **FlexCat**: For catalog and localization file handling. (https://github.com/adtools/flexcat)
*   **Identify**: For identifying various hardware components, including PCI devices. (https://codeberg.org/shred/identify)
*   `openpci.library`: For PCI device access.

## Contributing

We welcome contributions! Please feel free to fork the repository, make your changes, and submit a pull request.

