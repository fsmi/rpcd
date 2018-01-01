# rpcd - ratpoison control daemon

This project provides a limited interaction surface for (non-local) users,
allowing them to load layouts into the ratpoison window manager as well as
enabling them to start/stop a set of previously approved processes.

This allows non-privileged users to run eg. games or view videos on a big
screen without having to give them local shell access to start the software.

[![Coverity Scan Build Status](https://scan.coverity.com/projects/14561/badge.svg)](https://scan.coverity.com/projects/14561)

## Components

This project consists of several modules

### daemon

This tool interacts with ratpoison and the underlying Linux system, loading layouts
and managing processes. It provides an API via HTTP, which is detailed in [endpoints.txt](endpoints.txt)

### webclient

This module provides an interactive frontend to the daemon API, using HTML and JavaScript.
It can also be served from a machine other than the one providing the API.

### cli

The `cli` module provides a command line interface (`rpcd-cli`), which can be used for automating
interaction with `rpcd` servers.

## Build instructions

To build the daemon, the following prerequisites are needed

* libx11-dev
* GNU make
* A C compiler

Once those are met, running `make` in the root directory should suffice to build all the modules.

## Setup & Configuration

To install the web client, copy the directory to a path served by an HTTP daemon and modify
the API URL in [js/config.js](webclient/js/config.js) to your setup.

To install the daemon, simply create your configuration file and run the daemon executable
on the host running ratpoison (preferrably not as root). The first and only argument to the
daemon executable is the configuration file to be used.

To install the command line interface, run `make install` within the `cli` directory.

The daemon configuration file closely mirrors the standard `ini` file format. An example
configuration may be found in [daemon/rpcd.conf](daemon/rpcd.conf).
Sub-configuration files can be pulled in while parsing with an `include <file>` line.
Lines starting with a semicolon `;` are treated as comments and ignored. Inline comments
are not yet supported.

Some section types (`layout`, `command` and `x11`) describe named elements. This name is provided
in the section header, separated from the keyword by a space. Names are unique for a specific type,
except for layout names, which are unique per display.

| Section		| Option	| Default value		| Example value		| Description				| Notes
|-----------------------|---------------|-----------------------|-----------------------|---------------------------------------|------
|[api]			| bind		| none			| `10.23.0.1 8080`	| HTTP API host and port		|
|[x11 `name`]		| display	| `:0`			| `:0.0`		| X11 display identifier to use		|
|			| deflayout	| none			| `layout_name`		| Layout to apply on reset		|
|			| repatriate	| none			| `yes`			| Store current window-frame mapping	|
|[layout `name`]	| file		| none			| `path/to/file.sfdump` | Path to a ratpoison `sfdump`		| required
|			| read-layout	| none			| `yes`			| Read the layout data from a running `ratpoison`|
|[command `name`]	| description	| none			| `What does it do`	| Command help/description		|
|			| command	| none			| `/bin/echo %Var1`	| Command to execute including arguments| required
|			| windows	| none			| `no`			| Indicates that the command will not open an X window |
|			| `VariableName`| none			| `string Arg1`		| Command argument variable specification (see below) |

Commands may have any number of user-specifiable arguments, which replace the `%Variable` placeholders in the command specification.
Argument configuration is specified in command sections by assigning configuration to an option with the same name, ie to configure the
variable `%Var1`, you assign the specification to the option `Var1`.

Variables may either be of the type `enum`, providing a fixed set of values (separated by space characters) for the user to choose from,
or of the type `string`, allowing free-form user entry via a text field. For `enum` arguments, variable values exactly match the specified
options. For `string` arguments, an optional hint may be supplied, which is displayed as an entry hint within the web client.

Placeholders for which no argument configuration is specified are not replaced.

## Usage (Web Interface)

* To run a command
	* Point your browser to the web client
	* Select a command to see its options
	* Fill the options to your liking
	* Click `Run` to start the command
	* Click `Stop` to kill the program

* To change the layout
	* Point your browser to the web client
	* Select a layout to view it
	* Click `Apply` to activate it on the screen


## Usage (Command Line Interface)

The command line interface client for the HTTP API may for example be used for automating interaction with `rpcd`.
The module accepts the following parameters:

| Parameter 		| Example		| Description					|
|-----------------------|-----------------------|-----------------------------------------------|
|`--help`, `help`, `-?`|			| Print a short help summary			|
|`commands`		|			| List all commands supported by the server	|
|`layouts`		|			| List all layouts supported by the server	|
|`apply <layout>`	|`apply gpu/fullscreen`	| Load a layout on a display			|
|`run <command> <args>`|`run xecho Text=foo`	| Run a command with arguments, formatted as `key=value` |
|`stop <command>`	|`stop xecho`		| Stop a command				|
|`reset`		|			| Reset the `rpcd` instance, stopping all commands and loading the default layouts |
|`status`		|			| Query server status				|
|`--fullscreen`, `-F`	|			| Run a command in fullscreen mode		|
|`--frame <frame>`, `-f`| `-f gpu/0`		| Select a frame to run a command in		|
|`--json`, `-j`		|			| Print machine-readable JSON output		|
|`--host <host>`, `-h`	| `-h display-server`	| Select the target host (Default: `localhost`)	|
|`--port <port>`, `-p`	| `-p 8080`		| Select the API port (Default: `8080`)		|

## Usage (Daemon)

Interaction with the daemon is mostly limited to the HTTP API. It is documented in [endpoints.txt](endpoints.txt)
and may be accessed either directly or through the provided interfaces.

To generate a layout dump from an existing ratpoison instance for use with `rpcd`, run `ratpoison -c sfdump` and
store the output to a file.

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

### Commands without windows

Indicating that a command creates no X windows (for example, to implement control commands for another running
command) has the following consequences:

* The command will not have a `DISPLAY` variable in its environment
* The `frame`, `display` and `fullscreen` parameters are ignored
	* This implies that there will be no interaction with `ratpoison` at all for the command

### Default display

In a multihead environment (that is, one where `rpcd` manages multiple ratpoison instances on different
X servers), the specific display a command is to be run on needs to be passed. To stay compatible with single-head
deployments, this option may be omitted and the first display defined in the configuration is used.

In the same manner, layouts may still be defined with the old configuration syntax (omitting the display a layout
is defined on). The default display will then be used for the layout.

## Caveats

* Special characters and spaces in layout/command names currently cause problems. This may be fixed
in the (near) future.
* `string` arguments allow free-form user supplied data to be passed to spawned commands, presenting
a possible security risk if not properly sanitized. Properly checking and sanitizing user input is
the responsibility of the called command.
