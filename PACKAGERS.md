# Dear Packager,

This document is a quick summary of all you need to know to package imv for
your favourite operating system.

## Configuring `imv`

### 1. Select window systems to support

Your options here are Wayland or X11, or both. By default both are included,
with a separate binary for each being built. `/usr/bin/imv` will be a script
that checks for a Wayland compositor before running the appropriate binary.

If you only care about one of these, you can specify to build only one of these
by passing `-D windows=wayland` to `meson`, in which case only that
binary shall be packaged without the need for a launcher script to
select between the two.

Alternatively, you could provide separate packages for X11 and Wayland that
act as alternatives to each other.

### 2. Select backends to include

imv supports multiple "backends" in a plugin style architecture. Each backend
provides support for different image formats using different underlying
libraries. The ones that are right for your operating system depend on which
formats you need support for, and your licensing requirements.

imv is published under the MIT license, but its backends may have different
licensing requirements.

By default, the libraries available are detected and support for them is
automatically enabled, but you can enable or disable specific ones by
passing `-D libpng=enabled` or `-D libtiff=disabled`.
You can also make sure all the backends are enabled by passing
`-D auto_features=enabled`.

This fork always requires GIO for trash support and libpng >= 1.6 for crop
export. Disabling the PNG input backend with `-Dlibpng=disabled` does not
remove the libpng dependency.

## Building `imv`

Once your backends have been configured and you've confirmed the library
each backend uses is installed, you can simply follow the Installation section
of the [README](README.md) to build imv.

## Packaging `imv`

Package the resulting binary and man pages in your operating system's native
package format.

There is one desktop entry, `imv.desktop`. Directory browsing is built into
`imv`; the former `imv-dir` command, desktop entry, manual, and
`contrib-commands` build option have been removed.
