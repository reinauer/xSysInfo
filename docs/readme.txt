Short: Open Source System Information Program
Author: Stefan Reinauer
Uploader: Stefan Reinauer
Type: util/moni
Version: 0.2a
Requires: identify.library
Architecture: m68k-amigaos
Distribution: Freely Distributable

xSysInfo is an Open Source system information program for 68k-based classic
Amigas, and is an homage to the (in)famous and ubiquitous SysInfo program.

Additional features above and beyond SysInfo include:
- Memory benchmarking.  The tool now performs memory speed tests and reports
  bandwidth in MB/s, giving you a much clearer picture of your system's
  performance. It also includes refreshed baselines for reference systems to
  make comparisons more accurate.
- Accurate Floppy and Hard Disk performance.  No more overflow values of
  6,553,600 Bytes/sec.

Usage:
Start xSysInfo from Workbench, or run xSysInfo from the Shell.  With no
arguments it opens the graphical interface.

The System Software Installed tile defaults to an overview of the OS,
physical ROM, active ROM, Workbench, SetPatch, and graphics system. Use its
cycle button for the library, device, resource, and MMU lists. Total Chip
and Fast RAM are shown in the hardware overview.

Shell options:
  xSysInfo BRIEF
        Writes a compact benchmark summary to Shell output and does not open
        the graphical interface.
  xSysInfo FULL
        Runs the full benchmark/report pass and writes the complete text report
        to Shell output.  For example:
        xSysInfo FULL >RAM:xSysInfo.txt
  xSysInfo WHICH
        Writes a system report compatible with the WhichAmiga text format
        and does not open the graphical interface.
  xSysInfo DARK
        Opens the graphical interface with the dark blue palette.
  xSysInfo DEBUG
        Enables debug output.

If FULL is supplied with another output mode, FULL takes precedence. WHICH
takes precedence over BRIEF.

Workbench ToolTypes:
The supplied icon contains DISPLAY=auto.  Edit this ToolType to select where
the graphical interface opens:

  DISPLAY=auto
        Default.  Opens a window on RTG Workbench screens and otherwise opens
        xSysInfo on its own PAL or NTSC screen.
  DISPLAY=window
        Forces xSysInfo to open in a window.
  DISPLAY=screen
        Forces xSysInfo to open on its own screen.
  DARK
        Uses the dark blue palette.
  DEBUG
        Enables debug output.

Web page:
        https://github.com/reinauer/xSysInfo/blob/main/README.md
Latest archive distribution (lha):
        https://github.com/reinauer/xSysInfo/releases
Latest disk image distribution (adf):
        https://github.com/reinauer/xSysInfo/releases
Aminet:
        http://aminet.net/package/util/moni/xSysInfo
