# rpcd - ratpoison control daemon

This project provides a limited interaction surface for (non-local) users,
allowing them to load layouts into ratpoison and start/stop a set of previously
approved processes.

This allows non-privileged users to run eg. games on a big screen without having
to give them local shell access to start the software.

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
the API URL in `js/config.js` to your setup.

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

TODO

## Caveats

* Special characters and spaces in layout/command names currently cause problems. This may be fixed
the (near) future.
