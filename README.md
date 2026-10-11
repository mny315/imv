imv - X11/Wayland Image Viewer
==============================

`imv` is a command line image viewer intended for use with tiling window managers.

This [fork](https://github.com/mny315/imv) adds directory browsing by default,
trash with undo, and mouse cropping to
[Harry Jeffery's imv](https://sr.ht/~exec64/imv/).

Features
--------

* Native Wayland and X11 support
* Opening a single image also loads its directory for next/previous navigation
* Delete moves the current file to the system trash and selects the next image
* Ctrl+Z restores the last trashed file and jumps back to it; repeat for earlier files
* Ctrl+C starts a mouse crop; Enter saves the selection as a separate PNG, Esc cancels
* Image formats supported by optional backends include:
  * PNG
  * JPEG
  * WebP
  * Animated GIFs
  * SVG
  * TIFF
  * BMP, HEIF/AVIF, JPEG XL, QOI, and farbfeld
* Configurable key bindings and behaviour
* Highly scriptable with IPC via imv-msg

Trash undo history belongs to the current window and lasts until it is closed.
Crops are saved beside the original as `NAME-crop.png` (with a numeric suffix
if needed); existing files are never overwritten.

Example Usage
-------------

The following examples are a quick illustration of how you can use imv.
For detailed documentation see the man page.

    # Open an image and browse the other images in its directory
    imv photo.jpeg

    # Open an explicit list of images and directories
    imv image1.png another_image.jpeg a_directory

    # Opening a directory recursively
    imv -r Photos

    # Opening images via stdin
    find . -type f -name "*.svg" | imv

    # Open an image fullscreen
    imv -f image.jpeg

    # Viewing images in a random order
    find . -type f -name "*.png" | shuf | imv

    # Viewing images from stdin
    curl http://somesi.te/img.png | imv -

    # Viewing multiple images from the web
    curl -Osw '%{filename_effective}\n' 'http://www.example.com/[1-10].jpg' | imv

### Slideshow

imv can be used to display slideshows. You can set the number of seconds to
show each image for with the `-t` option at start up, or you can configure it
at runtime using the `t` and `T` hotkeys to increase and decrease the image
display time, respectively.

To cycle through a folder of pictures, showing each one for 10 seconds:

    imv -t 10 ~/Pictures/London

#### Custom configuration

imv's key bindings can be customised to trigger custom behaviour:

    [binds]

    # Also bind Shift+X to the same trash command as Delete
    <Shift+X> = trash

    # Rotate the currently open image by 90 degrees by pressing 'R'
    <Shift+R> = exec mogrify -rotate 90 "$imv_current_file"

    # Use dmenu as a prompt for tagging the current image
    u = exec echo "$imv_current_file" >> ~/tags/$(ls ~/tags | dmenu -p "tag")

### Scripting

With the default bindings, imv can be used to select images in a pipeline by
using the `p` hotkey to print the current image's path to stdout. The `-l` flag
can also be used to tell imv to list the remaining paths on exit for a "open
set of images, close unwanted ones with `x`, then quit imv to pass the
remaining images through" workflow.

Key bindings can be customised to run arbitrary shell commands. Environment
variables are exported to expose imv's state to scripts run by it. These
scripts can in turn modify imv's behaviour by invoking `imv-msg` with
`$imv_pid`.

For example:

    #!/bin/sh
    imv "$@" &
    imv_pid=$!

    while true; do
      # Some custom logic
      # ...

      # Close all open files
      imv-msg "$imv_pid" close all
      # Open some new files
      imv-msg "$imv_pid" open ~/new_path

      # Run another script against the currently open file
      imv-msg "$imv_pid" exec 'another-script.sh "$imv_current_file"'
      sleep 1
    done


Installation
------------

### Dependencies

| Library        |  Version |  Notes                                         |
|---------------:|:---------|------------------------------------------------|
| pthreads       |          | Required.                                      |
| inih           |          | Required. Meson can fetch it if unavailable.    |
| xkbcommon      |          | Required.                                      |
| pangocairo     |          | Required.                                      |
| gio-2.0        |          | Required. Provides system trash support.       |
| icu            |          | Default Unicode backend; libgrapheme is an alternative. |
| xxd            |          | Optional. Required for the original cmocka tests. |
| cmocka         |          | Optional. Required for the original cmocka tests. |
| X11            |          | Optional. Required for X11 support.            |
| GL             |          | Optional. Required for X11 support.            |
| xcb            |          | Optional. Required for X11 support.            |
| xkbcommon-x11  |          | Optional. Required for X11 support.            |
| wayland-client |          | Optional. Required for Wayland support.        |
| wayland-cursor |          | Optional. Required for Wayland support.        |
| wayland-egl    |          | Optional. Required for Wayland support.        |
| wayland-protocols |        | Optional. Required for Wayland support.        |
| wayland-scanner |         | Optional. Required for Wayland support.        |
| EGL            |          | Optional. Required for Wayland support.        |
| libtiff        |          | Optional. Provides TIFF support.               |
| libpng         | >= 1.6   | Required. Provides PNG export for cropping.    |
| libjpeg-turbo  |          | Optional. Provides JPEG support.               |
| LittleCMS      | 2        | Optional. Provides CMYK support for JPEGs.     |
| librsvg        | 2.44     | Optional. Provides SVG support.                |
| libnsgif       | 1.0.0    | Optional. Provides animated GIF support.       |
| libnsbmp       |          | Optional. Provides BMP support.                |
| libheif        | 1.13.0   | Optional. Provides HEIF support.               |
| libjxl         |          | Optional. Provides JPEGXL support.             |
| libwebp        |          | Optional. Provides WebP support.               |
| qoi            |          | Optional. Provides QOI support.                |

Dependencies are determined by which backends and window systems are enabled
when building `imv`. You can find a summary of which backends are available
in [meson_options.txt](meson_options.txt)

Build dependencies include a C99 compiler, Meson, Ninja, and pkg-config.
Asciidoc's `a2x` is optional for building the manual pages.

    meson setup build
    meson compile -C build
    meson test -C build --print-errorlogs
    meson install -C build

Before installation, run `./build/imv-wayland photo.png` or
`./build/imv-x11 photo.png`. For a single window system, configure with
`-Dwindows=wayland` or `-Dwindows=x11`; the executable is then `./build/imv`.
The installed `imv` launcher selects the window system automatically when
both are built.

`--prefix` controls installation prefix.  If more control over installation
paths is required, `--bindir`, `--mandir` and `--datadir` are
available.  Eg. to install `imv` to home directory, run:

    meson setup build --prefix="$HOME/.local"

### Tests

The regression tests use GLib, already provided by the required GIO dependency.
With the default `-Dtest=auto`, these tests run even if cmocka and xxd are absent.
Install both to also run the original test suite, or use `-Dtest=enabled` to
require them. `-Dtest=disabled` disables all tests.

Tests create temporary files and an isolated trash directory. Wayland input
tests do not require a running compositor. Local UNIX socket tests are skipped
if the execution environment forbids binding sockets.

To check memory handling:

    meson setup build-sanitize -Db_sanitize=address,undefined -Db_lundef=false
    meson test -C build-sanitize --print-errorlogs

License
-------
`imv`'s source is published under the terms of the [MIT](LICENSE) license.
