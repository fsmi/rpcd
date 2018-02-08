# rpcd - ratpoison control daemon

This project provides a limited interaction surface for (non-local) users,
allowing them to load layouts into the ratpoison window manager as well as
enabling them to start/stop a set of previously approved processes.

This allows non-privileged users to run eg. games or view videos on a big
screen without having to give them local shell access to start the software.

Additionally, it provides an automation controller to update the display contents
based on external factors.

[![Coverity Scan Build Status](https://scan.coverity.com/projects/14561/badge.svg)](https://scan.coverity.com/projects/14561)

## Components

This project consists of several modules

### daemon

This tool interacts with ratpoison and the underlying Linux system, loading layouts
and managing processes. It provides an external API via HTTP, which is detailed in [endpoints.txt](endpoints.txt).
Additionally, the automated control feature provides an optional internal API, which
is described in detail in a later section.

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
|`[api]`		| bind		| none			| `10.23.0.1 8080`	| HTTP API host and port		|
|`[control]`		| socket	| none			| `/tmp/rpcd`		| Unix domain socket for automation control | Created if missing
|			| fifo		| none			| `/tmp/rpcd-fifo`	| FIFO for automation control		| Created if missing
|`[variables]`		| `VariableName`| none			| `DefaultValue`	| Define an automation variable as well as its default value |
|`[automation]`		| Automation instructions	| none	| `assign foo bar/1`	| Automation control script (see below)	|
|`[x11 *name*]`		| display	| `:0`			| `:0.0`		| X11 display identifier to use		|
|			| deflayout	| none			| `layout_name`		| Layout to apply on reset		|
|			| repatriate	| none			| `yes`			| Store current window-frame mapping	|
|`[layout *name*]`	| file		| none			| `path/to/file.sfdump` | Path to a ratpoison `sfdump`		| Either `read-layout` or `file` is required
|			| read-layout	| none			| `yes`			| Read the layout data from a running `ratpoison`|
|`[command *name*]`	| description	| none			| `What does it do`	| Command help/description		|
|			| command	| none			| `/bin/echo %Var1`	| Command to execute including arguments| required
|			| windows	| none			| `no`			| Indicates that the command will not open an X window |
|			| chdir		| none			| `/home/foo/bar/`	| Working directory to execute the command in |
|			| `VariableName`| none			| `string Arg1`		| Command argument variable specification (see below) |
|`[window *name*]`	| command	| none			| `/bin/xecho %AutoVar`	| Command executed to start the window | required
|			| chdir		| none			| `/home/foo/baz`	| Working directory to start the window in |
|			| mode		| `lazy`		| `ondemand`		| Window swap/kill mode (see below)	|

### User commands
Commands may have any number of user-specifiable arguments, which replace the `%Variable` placeholders in the command specification.
Argument configuration is specified in command sections by assigning configuration to an option with the same name, ie to configure the
variable `%Var1`, you assign the specification to the option `Var1`.

Variables may either be of the type `enum`, providing a fixed set of values (separated by space characters) for the user to choose from,
or of the type `string`, allowing free-form user entry via a text field. For `enum` arguments, variable values exactly match the specified
options. For `string` arguments, an optional hint may be supplied, which is displayed as an entry hint within the web client.

Placeholders for which no argument configuration is specified are not replaced.

### Display automation
`rpcd` can be used to update the contents of the managed displays based on an automation script. This feature is optional.
Automated windows are only displayed when a display is not busy with user commands. The windows to be shown can be started and
stopped by `rpcd` automatically, based on the automation script and current variable values.

#### Automation variables
Automation variables occupy variable space distinct from user command variables. They may be set via the control inputs or the configuration
file. All spawned window processes receive the entire automation variable space in their environment. Variable arguments to `window`
processes also refer to the automated variable space.

Automation variables are required to follow the naming conventions for environment variables, that is the can only
consist of the ASCII characters A-z (upper- and lowercase), as well as the digits 0-9 and the underscore (*_*).
Variable names may not start a number, to be able to distinguish them from constants and may not contain spaces.

#### Automation scripting
The automation script, given in an `[automation]` section in the control file, consists of line-by-line instructions executed
sequentially when
	* `rpcd` is first started
	* A child (window or command) terminates
	* The `reset` API endpoint is invoked
	* An automated window process maps an X11 window

Changes are only made on displays that are not currently running any user commands.

The automation script may contain the following instructions:
| Instruction	| Arguments	| Example		| Description						|
|---------------|---------------|-----------------------|-------------------------------------------------------|
| `default`	| Display name	| `default gpu`		| Apply the default layout on a display if set		|
| `layout`	| Layout name	| `layout gpu/foo`	| Activate a layout					|
| `assign`	| Command, frame| `assign bar gpu/1`	| Assign an automated window to a frame			|
| `skip`	| # Instructions| `skip 5`		| Skip the next `n` instructions			|
| `done`	| -		| `done`		| Terminate automation script execution			|
| `if`		| Conditional	| `if empty baz, done`	| If condition is not met, skip the next statement	|

Conditionals may be negated using the syntax `if not`. Valid conditional expressions are
* `a < b`: Expression `a` numerically less than `b`
* `a > b`: Expression `a` numerically greater than `b`
* `a = b`: Expression `a` contains same string as `b`
* `empty a`: Expression `a` contains an empty string

Expressions may either be
* Automation variable names
* Numeric constants
* Strings encapsulated in double quotes (*"*)

For each display, the last `default`/`layout` instruction decides the final layout to be applied, overwriting previous
layout calls. In a similar fashion, later `assign` calls overwrite earlier ones referring to the same display/frame combination
or window.

Note that cascading conditional statements, while possible, will not work as intended due to the internal implementation
of the automation engine.

When assigning a window active on one display to a frame on another display, `rpcd` will stop and restart the window
process to execute it on the new display. When defining a window with the `ondemand` kill-mode (see below), the process
is terminated as soon as the window is no longer mapped on any display.

#### Automation input
External processes may update the automation variable space and trigger a re-evaluation of the automation script by using one of the
configured control inputs (Unix domain socket or FIFO).

To update a variable, write or send a *`\n`*-terminated line of the following format:

```
VariableName=Value
```

#### Automated windows
Variables (for example `%AutoVar`) used as parameters are replaced with their value (from the automation variable space) before execution.
Variables reflect the content they had when the window was started, as updates at a later time are not possible.
A method to restart a window on variable change may be implemented in the future.

The following swap/kill modes are supported for windows:

* default (`lazy`): The process is started when required, and stopped only when when shutting down or mapping the window to another X server (necessitating an update of the environment).
* `ondemand`: Start the process when the window is mapped, terminate the process when it is unmapped.
* `keepalive`: Start the process on `rpcd` startup (on the default display), stop only when shutting down or switching X servers.

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

### Reloading the daemon configuration
When the daemon receives `SIGHUP`, it will try to reload the configuration file it was started with.
When a user command is running, the configuration reload is postponed until the command terminates.
Upon receiving a second `SIGHUP` while waiting for command termination, the configuration is force-reloaded,
which will terminate all running commands.

Should the reloaded configuration contain an error, `rpcd` will not shut down but respond as if no
configuration would have been read. This implies that the daemon will not respond on any API endpoint.

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
* The command will not block the execution of any automation script

### Default display

In a multihead environment (that is, one where `rpcd` manages multiple ratpoison instances on different
X servers), the specific display a command is to be run on needs to be passed. To stay compatible with single-head
deployments, this option may be omitted and the first display defined in the configuration is used.

In the same manner, layouts may still be defined with the old configuration syntax (omitting the display a layout
is defined on). The default display will then be used for the layout.

### Mapping windows to processes

There is no inherently reliable way for an external process like `rpcd` to map X11 window IDs to the processes
that spawned them. This is a known problem, for which [the `_NET_WM_PID` protocol](https://specifications.freedesktop.org/wm-spec/1.3/ar01s05.html)
was created. For child processes (commands and automated windows) supporting this protocol (which is most),
all features work as intended. For processes not supporting it, a "window filter" feature is intended to be implemented
at a later stage. Sadly, some applications refuse to set any identifying information within the window properties.
If a user command is running, `rpcd` may assign such a window to the last command started as a last-resort heuristic.
The following features may be broken for such processes:

* Loading a layout while the command is running may hide the window without terminating the command
* Display automation may not run even though there is no window displayed, as the display is still considered busy until the command terminates
* The window may not show up at all (with this not being recognized as a bug in `rpcd`)

## Caveats

* Nesting conditionals in the automation configuration will pass the parser, but execution will
not be straightforward and may produce unintended side effects due to the internal implementation.
* Special characters and spaces in layout/command names currently cause problems. This may be fixed
in the (near) future.
* `string` arguments allow free-form user supplied data to be passed to spawned commands, presenting
a possible security risk if not properly sanitized. Properly checking and sanitizing user input is
the responsibility of the called command.
* All programs started by `rpcd` *should* support [the `_NET_WM_PID` protocol](https://specifications.freedesktop.org/wm-spec/1.3/ar01s05.html)
for `rpcd` to be able to reliably map X11 windows to child processes.
