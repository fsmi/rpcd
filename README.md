# rpcd - ratpoison control daemon

This project provides a limited interaction surface for (non-local) users,
allowing them to load layouts into the ratpoison window manager as well as
enabling them to start/stop a set of previously approved processes.

This allows non-privileged users to run eg. games or view videos on a big
screen without having to give them local shell access to start the software.

## Components

This project consists of several modules

### daemon

This tool interacts with ratpoison and the underlying Linux system, loading layouts
and managing processes. It provides an API via HTTP, which is detailed in [endpoints.txt](endpoints.txt)

### webclient

This module provides an interactive frontend to the daemon API, using HTML and JavaScript.
It can also be served from a machine other than the one providing the API.

## Build instructions

To build the daemon, the following prerequisites are needed

* libx11-dev
* GNU make
* A C compiler

Once those are met, running `make` in the daemon directory should suffice.

## Setup & Configuration

To install the web client, copy the directory to a path served by an HTTP daemon and modify
the API URL in [js/config.js](webclient/js/config.js) to your setup.

To install the daemon, simply create your configuration file and run the daemon executable
on the host running ratpoison (preferrably not as root). The first and only argument to the
daemon executable is the configuration file to be used.

The daemon configuration file closely mirrors the standard `ini` file format. An example
configuration may be found in [daemon/rpcd.conf](daemon/rpcd.conf).

To generate a layout dump from an existing ratpoison instance, run `ratpoison -c sfdump`.

## Usage

* Point your browser to the web client
* Select a layout to view it
* Load a layout to apply it to the screen
* Select a command to see its options
* Fill the options to your liking
* Click `Run` to start the command
* Click `Stop` to kill the program

## Background

Some of the features may not be immediately obvious or may have interesting background information.

### Process termination

When sending a `stop` command to the API, rpcd first sends SIGTERM to the process group it spawned for the
`start` command. This should terminate all processes within that group. Should a process misbehave and not
terminate upon receiving SIGTERM, the next `stop` command will send SIGKILL to the process.

### Fullscreen checkbox

The fullscreen checkbox (and the fullscreen parameter to the `start` endpoint) cause rpcd to execute the
ratpoison command `only` (take window fullscreen on this screen) before running the command, as well as
executing `undo` at termination, rolling back the last layout change.

## Caveats

* Special characters and spaces in layout/command names currently cause problems. This may be fixed
the (near) future.
