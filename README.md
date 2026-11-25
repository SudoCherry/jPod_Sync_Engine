# jPod_Sync_Engine
A program that allows POSIX based operating systems to sync with iPods (Non-touch Models)

jPod Sync Engine is an experimental command-line project focused on
understanding and implementing the processes involved in managing music
on classic iPods. It is being developed in modular C, with separate
components for music-library discovery, metadata extraction, iPod file
handling, and iTunesDB parsing and modification.

The project is currently a **work in progress**. Several low-level
components and standalone tools are implemented, but the main
application does not yet perform a complete end-to-end synchronisation.

> \[!WARNING\] This is an early prototype. Do not rely on it as your
> only copy of a music library, and do not experiment with
> database-writing or file-removal operations on an iPod containing data
> you cannot afford to lose. Back up the device first.

## Contents

-   [Project goals](#project-goals)
-   [Current status](#current-status)
-   [Features and components](#features-and-components)
-   [Requirements](#requirements)
-   [Building](#building)
-   [Running](#running)
-   [Command-line tools](#command-line-tools)
-   [Tests](#tests)
-   [Project structure](#project-structure)
-   [Implementation notes](#implementation-notes)
-   [Limitations](#limitations)
-   [Roadmap](#roadmap)
-   [Contributing](#contributing)
-   [License](#license)

## Project goals

The long-term goal is to provide a small, understandable alternative
workflow for managing music on compatible classic iPods, without
depending on iTunes.

Development is organised around a few principles:

-   **Modularity:** keep device handling, library scanning, metadata,
    file copying and database work in separate components.
-   **Safety:** validate database structures, avoid writing incomplete
    output files, and keep destructive operations explicit.
-   **Portability:** use standard C and common system interfaces where
    practical.
-   **Understandability:** keep the implementation approachable and
    testable rather than hiding the process behind a large framework.

## Current status

The repository contains working lower-level components and test
programs. The main interactive application is not yet a complete
synchronisation tool.

  -----------------------------------------------------------------------
  Area                                Current state
  ----------------------------------- -----------------------------------
  Interactive command-line            Implemented; basic menu and
  application                         configuration

  Music-folder configuration          Implemented; saves the selected
                                      folder in
                                      `~/.untitled_sync_engine.conf`

  Music-library scanning              Implemented; recursively finds
                                      `.mp3` and `.m4a` files

  MP3 metadata                        Implemented; reads ID3v2.2,
                                      ID3v2.3, ID3v2.4 and ID3v1 tags

  Filename metadata fallback          Implemented

  iPod device state model             Implemented; connection and
                                      identification are currently
                                      simulated in the main application

  iTunesDB reading and inspection     Implemented

  Track file copying and removal      Implemented as a separate component
                                      and demo tool

  MP3 technical information           Implemented for MPEG Layer III
                                      files

  iTunesDB writing                    Implemented for database layouts
                                      supported by the writer; includes
                                      verification

  Automatic USB / FireWire detection  Not implemented

  Complete add / remove / synchronise Not implemented
  workflow in the main application    
  -----------------------------------------------------------------------

The table describes the code in this prototype, not a claim of
compatibility with every iPod model or database version.

## Features and components

### Music library

The library scanner recursively searches a selected music directory for
`.mp3` and `.m4a` files. It stores each discovered track's path, file
size, title, artist and album in a dynamically allocated list.

For MP3 files, metadata can be read from ID3 tags. If useful metadata is
unavailable, the filename parser can make a basic guess. The filename
convention `Artist - Title.mp3` is recognised.

### iTunesDB reader and inspector

The database module can read an iPod `iTunesDB` file and expose its
track entries, including identifiers, metadata and device paths. It also
provides a structural inspection function intended to help examine real
database files during development.

The reader is read-only.

### iTunesDB writer

The database writer can add a track to a supported existing iTunesDB
layout. It checks the input before modifying anything, writes to a
separate output path, validates the result, and uses a temporary output
file before renaming it into place.

The writer deliberately rejects layouts it does not understand. This is
a compatibility safeguard, not a guarantee that every accepted database
is safe for every iPod.

### Track file handling

The copier can place a music file into an existing iPod music directory
structure, using the familiar `F##` folders and generated filenames. It
does not create the iPod music folders.

The copy operation writes to a temporary filename and renames the file
after the copy completes. A companion function can remove a previously
copied track when given its database-style path, and refuses paths
outside the iPod music directory.

### Device model

The device module defines connection states and stores basic device
information, including model ID, generation, year, capacity, connection
type and serial number.

At present, the main program uses a temporary simulated connection for
development. It does not discover a physical iPod automatically.

## Requirements

-   GCC or another compatible C compiler
-   GNU Make
-   A Unix-like environment with the system interfaces used by the
    source code

The supplied Makefile uses GCC and C11:

``` make
-std=c11 -Wall -Wextra -pedantic
```

No third-party library dependency is declared by the supplied Makefile.
Platform-specific behaviour may still need adjustment when building on a
different operating system.

## Building

Clone or download the repository, then open a terminal in the project
directory (the directory containing the `Makefile`).

Build the main application:

``` sh
make
```

This produces the `sync` executable.

Build the standalone development tools:

``` sh
make tools
```

Build and run the test suite:

``` sh
make test
```

Remove generated object files, executables, tests and tools:

``` sh
make clean
```

## Running

Start the interactive application:

``` sh
./sync
```

The menu currently provides these options:

  Option   Purpose
  -------- ------------------------------------------------------------
  `H`      Display help
  `M`      Choose and save the music folder
  `S`      Select the FireWire sync option (workflow not implemented)
  `Y`      Select the USB sync option (workflow not implemented)
  `A`      Scan the configured music folder
  `R`      Select the remove-tracks option (workflow not implemented)
  `Q`      Quit

The music-folder path is saved in:

``` text
~/.untitled_sync_engine.conf
```

The first time the application is run, choose a valid music folder with
`M`. The `A` option currently scans the folder and reports discovered
tracks; it does not copy them to an iPod.

### Development-only menu options

The application also contains temporary debugging options:

-   `C` simulates connecting and identifying a USB iPod.
-   `D` simulates disconnecting the device.
-   `L` reads an iTunesDB file and lists its tracks.
-   `I` inspects an iTunesDB file's structure.

These options are for development and testing, not automatic device
detection.

## Command-line tools

The Makefile can build two standalone tools.

### `copier_demo`

Copies one music file into an existing iPod music-folder structure:

``` sh
./copier_demo <ipod_root> <music_file>
```

For example:

``` sh
./copier_demo "/Volumes/IPOD" "./music/example.mp3"
```

The iPod root should contain an existing `iPod_Control/Music/` directory
with `F##` folders inside it. The tool prints both the resulting
filesystem path and the database-style path.

### `dbwrite_demo`

Verify an iTunesDB file:

``` sh
./dbwrite_demo verify <iTunesDB>
```

Add an MP3 track to a new output database:

``` sh
./dbwrite_demo add <in_iTunesDB> <out_iTunesDB> <song.mp3> <ipod_path>
```

The `ipod_path` uses the database-style path format, for example:

``` text
:iPod_Control:Music:F07:ABCD.mp3
```

The input database is not modified; the writer produces a separate
output file. The demo reads the output again and runs the writer's
verification checks.

## Tests

Run all component tests with:

``` sh
make test
```

The Makefile currently defines tests for:

-   Device state handling (`test_device`)
-   Music library scanning (`test_library`)
-   iTunesDB reading (`test_database`)
-   Configuration loading and saving (`test_config`)
-   Metadata parsing (`test_tags`)
-   Track copying and removal (`test_copier`)
-   MP3 technical information (`test_mp3info`)
-   iTunesDB writing and verification (`test_dbwriter`)

Each test is built as a separate executable. The `make test` target runs
them in sequence and stops if one fails.

## Project structure

The project is split into small modules. The principal source and header
files are:

  -----------------------------------------------------------------------
  Files                               Responsibility
  ----------------------------------- -----------------------------------
  `main.c`                            Interactive command-line interface

  `ipod.h`, `device.c`, `device.h`    Device information, connection
                                      types and state transitions

  `library.c`, `library.h`            Recursive music-folder scanning and
                                      track collection

  `tags.c`, `tags.h`                  ID3 metadata reading and
                                      filename-based fallback

  `textconv.c`, `textconv.h`          Text conversion helpers used by
                                      metadata/database code

  `database.c`, `database.h`          iTunesDB reading and inspection

  `dbwriter.c`, `dbwriter.h`          Adding tracks to supported iTunesDB
                                      layouts and verifying output

  `copier.c`, `copier.h`              Copying and removing music files in
                                      the iPod music directory

  `mp3info.c`, `mp3info.h`            MP3 file size, duration, bitrate
                                      and stream information

  `config.c`, `config.h`              Persistent music-folder
                                      configuration

  `Makefile`                          Build, test, tools and clean
                                      targets
  -----------------------------------------------------------------------

The repository also contains individual test sources, fixture-generation
support and small demonstration programs.

## Implementation notes

-   The library scanner recognises `.mp3` and `.m4a` files, but the
    current tag reader documents ID3 support for MP3 files. M4A metadata
    extraction is not implemented.
-   MP3 technical information is currently limited to MPEG Layer III.
-   The database writer supports only the iTunesDB layouts it explicitly
    understands. Unsupported layouts are rejected without writing an
    output database.
-   The copier expects the target iPod music folders to exist already.
-   The main application's device connection is simulated; the project
    does not currently perform physical USB or FireWire discovery.
-   The main menu's add, remove and sync workflows are placeholders. The
    standalone components and demos should not be mistaken for a
    completed integrated workflow.

## Roadmap

The following are potential development milestones, not promises of
completed functionality:

-   [ ] Implement physical iPod detection and identification.
-   [ ] Integrate track copying, metadata extraction and database
    writing into the main application.
-   [ ] Implement a complete add-tracks workflow.
-   [ ] Implement safe track removal and database updates.
-   [ ] Implement end-to-end USB synchronisation.
-   [ ] Investigate and implement FireWire support for compatible
    devices.
-   [ ] Expand compatibility testing across iPod models and real-world
    iTunesDB files.
-   [ ] Improve user-facing error reporting and recovery.

## Contributing

This is an experimental project, and feedback, bug reports and code
contributions are welcome.

Before submitting a change:

1.  Keep components focused and modular.
2.  Add or update tests for changed behaviour.
3.  Run `make test`.
4.  Clearly document any device models, database layouts or
    operating-system assumptions involved.

When reporting a compatibility issue, include the relevant iPod
model/generation, connection type, operating system, and the exact error
message where possible. Do not share personal music files or private
device identifiers in public issue reports. I am looking for people to
help me maintain and upgrade this, but I am not looking for slopcoders.

## License

jPod Sync Engine is licensed under the GNU General Public License v3.0 (GPL-3.0).

You are free to use, study, modify and redistribute this software under the terms of the GPL-3.0. Any distributed modified versions must comply with the license's requirements.

The full license text is available at https://www.gnu.org/licenses/gpl-3.0.en.html
------------------------------------------------------------------------

-   jPod Sync Engine --- an ongoing exploration of classic iPod music
    synchronisation.\*
