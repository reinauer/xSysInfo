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
Start xSysInfo from its Workbench icon or run it from the Shell. With no
command-line options, it opens the graphical interface.

Command-line options:
Use xSysInfo [options]. Options are case-insensitive keywords, entered
without a leading dash.

  Option  Description
  ------  --------------------------------------------------------------
  BRIEF   Run benchmarks and write a compact summary to Shell output.
  FULL    Run CPU, memory, and drive benchmarks and write the complete
          text report to Shell output.
  WHICH   Write a column-aligned system report compatible with the
          WhichAmiga 1.3.3 text format to Shell output.
  DARK    Use the dark blue palette in the graphical interface.
          The default is the gray palette.
  DEBUG   Enable diagnostic output. Debugging is off by default.

BRIEF, FULL, and WHICH run without opening the graphical interface.
If more than one report mode is supplied, FULL takes precedence over
WHICH, which takes precedence over BRIEF. DEBUG can be combined with
any mode; DARK affects only the graphical interface.

Examples:
  xSysInfo DARK
      Open the graphical interface in dark mode.
  xSysInfo FULL >RAM:xSysInfo.txt
      Save the full report to a file in RAM:.

Graphical interface:
The System Software Installed tile defaults to an overview of the OS,
physical ROM, active ROM, Workbench, SetPatch, and graphics system.
Use its cycle button for the library, device, resource, and MMU lists.
Total Chip and Fast RAM are shown in the hardware overview.

Workbench icon ToolTypes:
These settings come from the program's icon when xSysInfo starts from
Workbench. Edit the icon's ToolTypes, with one entry per line.
The supplied icon contains DISPLAY=auto.

  ToolType        Description
  --------------  ------------------------------------------------------
  DISPLAY=auto    Default. Open a window if the Workbench screen
                  is wider than 640 pixels or taller than 512 pixels;
                  otherwise use a separate PAL or NTSC screen.
  DISPLAY=window  Open a window on the Workbench screen.
  DISPLAY=screen  Open a separate PAL or NTSC screen.
  DARK            Use the dark blue palette. Omit this entry to use the
                  default gray palette.
  DEBUG           Enable diagnostic output. Omit this entry to leave
                  debugging off.

Use one DISPLAY entry to select the display mode. Add DARK or DEBUG
as separate entries to enable either option.

Web page:
        https://github.com/reinauer/xSysInfo/blob/main/README.md
Latest archive distribution (lha):
        https://github.com/reinauer/xSysInfo/releases
Latest disk image distribution (adf):
        https://github.com/reinauer/xSysInfo/releases
Aminet:
        http://aminet.net/package/util/moni/xSysInfo
