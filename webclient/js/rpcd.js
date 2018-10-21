class Controller {
	ajax(url, method, payload) {
		return new Promise(function(resolve, reject) {
			var request = new XMLHttpRequest();
			request.onreadystatechange = function() {
				if (request.readyState === XMLHttpRequest.DONE) {
					switch(request.status) {
						case 200:
							document.querySelector('#status').classList.remove('api-error');
							try {
								var content = JSON.parse(request.responseText);
								resolve(content);
							} catch(err) {
								reject(err);
							}
							break;
						case 400:
							try {
								var content = JSON.parse(request.responseText);
								reject(content.status);
							} catch (err) {
								reject(err);
							}
							break;
						case 0:
							document.querySelector('#status').classList.add('api-error');
							if (window.location.protocol === 'https:') {
								reject('The API is not available via HTTPS, please connect via HTTP');
							} else {
								reject('Failed to access API');
							}
							break;
						default:
							reject(`${request.status}: ${request.statusText}`);
					}
				}
			};
			console.log('Request:', url, method, JSON.stringify(payload));
			request.open(method, url);
			request.send(JSON.stringify(payload));
		});
	}

	status(text) {
		document.querySelector('#status').textContent = text;
	}

	reset() {
		this.ajax(`${window.config.api}/reset`, 'GET').then(
			() => {
				this.status('Reset successful');
				this.getStatus();
			},
			(err) => {
				this.status(`Reset encountered an error: ${err}`);
				this.getStatus();
			});
	}

	stopCommand(i) {
		this.ajax(`${window.config.api}/stop/${this.commands[i].name}`, 'GET').then(
			() => {
				this.status('Command stopped');
				this.getStatus();
			},
			(err) => {
				this.status(`Failed to stop command: ${err}`);
				this.getStatus();
			});
	}

	inputRadio(i, prefix, listener) {
		let input = document.createElement('input');
		input.value = i;
		input.type = 'radio';
		input.name = `${prefix}s`;
		input.id = `${prefix}_${i}`;
		input.addEventListener('change', listener);
		return input;
	}

	fillList(list, elem, prefix, changeListener) {
		elem.innerHTML = '';
		list.forEach((item, i) => {
			let li = document.createElement('li');
			li.id = `li_${prefix}_${i}`;
			let input = this.inputRadio(i, prefix, changeListener)
			li.appendChild(input);

			let label = document.createElement('label');
			label.setAttribute('for',`${prefix}_${i}`);
			label.textContent = item.name;
			li.appendChild(label);
			let button;

			if (prefix === 'command') {
				button = this.createButton(`${prefix}_${i}_stop`,
					'\u25A0', () => {
						this.stopCommand(i);
					}, 'command-button');
				button.classList.add('stop')
				button.classList.add('hidden');
				li.appendChild(button);
				button = this.createButton(`${prefix}_${i}_start`,
					'\u25B8', () => {
						this.startCommand(i);
					}, 'command-button');
			} else {
				button = this.createButton(`${prefix}_${i}_button`,
					'apply', this.applyLayout.bind(this));
			}

			li.appendChild(button);
			elem.appendChild(li);
			if (i === 0) {
				input.checked = 'true';
				input.dispatchEvent(new Event('change'));
			}
		});
	}

	createButton(id, text, listener, css_class) {
		let span = document.createElement('span');
		span.classList.add('elem-button');
		if (css_class) {
			span.classList.add(css_class);
		}
		span.id = id;
		span.textContent = text;
		span.addEventListener('click', listener);
		return span;
	}

	applyLayout() {
		let layout = document.querySelector('input[name="layouts"]:checked').value;
		let display = document.querySelector('#display_selector').value;

		let d = this.layouts.find((d) => {
			return d.display === display;
		});

		this.ajax(`${window.config.api}/layout/${display}/${d.layouts[layout].name}`, 'GET').then(
			() => {
				this.status('Layout loaded successfully');
				this.state.layout = layout;
				this.getStatus();
			},
			(err) => {
				this.status(`Failed to apply layout: ${err}`);
				this.getStatus();
			}
		);
	}

	startCommand(i) {
		let command = this.commands[i];
		this.status(`Start command: ${i}`);

		let radio = document.querySelector('input[name="commands"]:checked');
		if (command.args.length > 0 && radio.value !== `${i}`) {
			this.status(`First enter arguments.`);
			let target = document.querySelector(`#command_${i}`);
			target.checked = true;
			target.dispatchEvent(new Event('change'));
			return;
		}

		let args = {};

		let ret = command.args.every((val, i) => {
			let elem = document.querySelector(`#arg_${i}`);
			switch(val.type) {
				case 'string':
					args[val.name] = elem.value;
					break;
				case 'enum':
					if (val.options.indexOf(elem.value) < 0) {
						this.status(`Value not in enum range for argument ${val.name}.`);
						return false;
					} else {
						args[val.name] = elem.value;
					}
					break;
			}
			return true;
		});

		if (!ret) {
			return;
		}
		let options = {
			arguments: args
		};
		let values = document.querySelector('#cmdFrame').value.split('/');
		if (command.windows && command.windows > 0) {
			options.fullscreen = document.querySelector('#fullscreen').checked ? 1 : 0;
			options.frame = parseInt(values[1], 10);
			options.display = values[0];
		}

		this.ajax(`${window.config.api}/command/${command.name}`, 'POST', options).then(
		(ans) => {
			this.status('Command started');
			this.getStatus();
		},
		(err) => {
			this.status(err);
			this.getStatus();
		});
	}

	displayChanged() {
		let d = document.querySelector('#display_selector').value;

		let display = this.layouts.find((val) => {
			return val.display === d;
		});

		if (display) {
			this.fillList(display.layouts,
						document.querySelector('#layout_items'),
						'layout',
						this.layoutChanged.bind(this));
		}
	}

	layoutChanged(event) {

		let display = document.querySelector('#display_selector').value;

		let d = this.layouts.find((d) => {
			return d.display === display;
		});

		let layout = d.layouts[event.target.value];
		let canvas = document.querySelector('#previews');
		canvas.innerHTML = '';
		let screens = {};
		this.canvases = [];

		layout.screens = layout.screens || [];
		layout.screens.forEach((screen) => {
			let div = document.createElement('div');
			let c = document.createElement('canvas');
			c.addEventListener('dragover', (e) => {
				if (e.preventDefault) {
					e.preventDefault();
				}
				e.dataTransfer.dropEffect = 'copy';
    			return false;
			});
			c.addEventListener('drop', (e) => {
				this.dropEvent(e, c, screen);
			});
			c.width = screen.width;
			c.height = screen.height;
			div.appendChild(c);
			canvas.appendChild(div);
			let ctx = c.getContext('2d');
			ctx.lineWidth = 10;
			ctx.strokeStyle = '#000000';
			ctx.font = '10em Georgia';
			ctx.textAlign = 'center';
			ctx.textBaseline = 'middle';
			screens[screen.id] = ctx;
		});

		layout.frames = layout.frames || [];
		layout.frames.forEach((frame) => {
			let ctx = screens[frame.screen];
			if (ctx) {
				ctx.fillStyle = '#FFFFFF';
				ctx.globalAlpha = 0.8;
				ctx.fillRect(frame.x, frame.y, frame.w, frame.h);
				ctx.stroke();
				ctx.globalAlpha = 1.0;
				ctx.fillStyle = '#000000';
				ctx.rect(frame.x, frame.y, frame.w, frame.h);
				ctx.stroke();
				ctx.fillText(`${frame.id}`,
					frame.x + (frame.w / 2),
					frame.y + (frame.h / 2));
			} else {
				console.log(`screen ${frame.screen} not defined (found in frame ${frame.id}).`);
			}
		});
	}

	fillFrameBox() {
		let box = document.querySelector('#cmdFrame');
		let oldvalue = box.value;
		box.innerHTML = '';
		this.state.layout.forEach((display, i) => {
			let d = this.layouts.find((d) => {
				return d.display === display.display;
			});

			if (!d) {
				return;
			}
			let layout = d.layouts.find((l) => {
				return l.name === display.layout;
			});
			if (!layout) {
				return;
			}
			layout.frames.forEach((frame, k) => {
				let option = document.createElement('option');
				option.value = `${d.display}/${frame.id}`;
				if (option.value === oldvalue) {
					option.selected = true;
				}
				option.textContent = `${d.display}/${frame.id}`;
				box.appendChild(option);
			});
		});
	}

	cmdChanged(event) {
		let cmd = this.commands[event.target.value];

		document.querySelector('#cmdHeadline').textContent = cmd.name;
		document.querySelector('#cmdDescription').innerText = cmd.description;
		this.fillFrameBox();

		let cmd_args = document.querySelector('#cmd_args');
		cmd_args.innerHTML = '';

		cmd.args.forEach((option, i) => {
			let li = document.createElement('li');

			let label = document.createElement('label');
			label.setAttribute('for', `arg_${i}`);
			label.textContent = `${option.name}:`;
			let input;
			switch(option.type) {
				case 'enum':
					input = document.createElement('select');
					option.options.forEach((item) => {
						let o = document.createElement('option');
						o.value = item;
						o.textContent = item;
						input.appendChild(o);
					});
					break;
				case 'string':
				default:
					input = document.createElement('input');
					input.type = 'text';
					input.placeholder = option.hint;
					break;
			}
			input.id = `arg_${i}`;
			input['data-option'] = option;
			li.appendChild(label);
			li.appendChild(input);

			cmd_args.appendChild(li);
		});
	}

	buildRunningCommand(elem, index) {

		let item = document.createElement('li');
		item.draggable = true;
		item.addEventListener('dragstart', (e) => {
			this.selectLayoutFrame();
			e.dataTransfer.effectAllowed = 'copy';
			e.dataTransfer.setData('cmd_name', elem.name); // required otherwise doesn't work
		});
		let name = document.createElement('span');
		name.textContent = elem.name;
		let close = document.createElement('span');
		close.classList.add('breadcrumb_button');
		close.textContent = ' [x]';
		close.addEventListener('click', () => {
			this.stopCommand(index);
		});

		item.appendChild(name);
		item.appendChild(close);

		return item;
	}

	dropEvent(e, canvas, screen) {
		e.stopPropagation();
		let display_selected = document.querySelector('#display_selector').value;
		let layouts_selected = document.querySelector('input[name="layouts"]').value;
		let state_layout = this.state.layout.find((val) => {
			return val.display === display_selected;
		});
		if (!state_layout) {
			console.error(`Cannot find layout for display ${display_selected}` )
			return
		}

		state_layout = state_layout.layout;


		let rect = canvas.getBoundingClientRect();
		let co = {
    		x: e.clientX - rect.left,
    		y: e.clientY - rect.top
    	};

		let x_scale = screen.width / canvas.clientWidth;
		let y_scale = screen.height / canvas.clientHeight;

		let xScreen = x_scale * co.x;
		let yScreen = y_scale * co.y;

		let display = this.layouts.find((val) => {
			return val.display === display_selected;
		});

		let layout = display.layouts.find((val) => {
			return val.name === state_layout;
		});

		if (!layout) {
			console.error(`Cannot find layout with name ${state_layout}`);
			return;
		}

		let frame = layout.frames.find((val) => {
			if (val.screen !== screen.id) {
				return false;
			}
			if (xScreen < val.x || xScreen > val.x + val.w) {
				return false;
			} else if (yScreen < val.y || yScreen > val.y + val.h) {
				return false;
			}
			return true;
		});

		if (!frame) {
			return;
		}

		let cmd = e.dataTransfer.getData('cmd_name');

		this.ajax(`${window.config.api}/move/${cmd}/${frame.id}`, 'GET')
			.then(() => {
				this.status(`Moved command ${cmd} to frame ${frame.id}`);
			},
			(err) => {
				this.status(`Failed to query status: ${err}`);
				console.log(`Failed to query status: ${err}`);
		});
	}

	getStatus(first) {
		this.ajax(`${window.config.api}/status`, 'GET')
			.then((state) => {
				this.state = state;
				this.fillFrameBox();

				if (state.running) {
					let run_cmds = document.querySelector('#running_commands');
					run_cmds.innerHTML = '';
					this.commands.forEach((elem, index) => {
						let id = state.running.findIndex((val) => {
							return val === elem.name;
						});

						let elemStart = document.querySelector(`#command_${index}_start`);
						let elemStop = document.querySelector(`#command_${index}_stop`);
						let label = document.querySelector(`#command_${index} + label`);
						if (id < 0) {
							label.classList.remove('running');
							elemStop.classList.add('hidden');
							elemStart.classList.remove('hidden');
						} else {
							run_cmds.appendChild(this.buildRunningCommand(elem, index));
							label.classList.add('running');
							elemStart.classList.add('hidden');
							elemStop.classList.remove('hidden');
						}
					});
				}
				if (first && document.querySelector('#tabl').checked) {
					this.selectLayoutFrame();
				}
			},
			(err) => {
				this.status(`Failed to query status: ${err}`);
				console.log(`Failed to query status: ${err}`);
		});
	}

	constructor() {
		this.layouts = [];
		this.commands = [];
		this.state = {
			layout: [],
			running: [0]
		};

		// dummies
		let lp = new Promise((resolve, reject) => {
			this.ajax(`${window.config.api}/layouts`, 'GET')
				.then((layouts) => {
					this.layouts = layouts;
					let display = document.querySelector('#display_selector');
					display.innerHTML = '';
					this.layouts.forEach((item, i) => {
						let option = document.createElement('option');
						option.value = item.display;
						option.textContent = item.display;
						display.appendChild(option);
					});
					this.displayChanged();
					resolve(layouts);
				},
				(err) => {
					this.status(err);
					reject(err);
				});
		});
		let cp = new Promise((resolve, reject) => {
			this.ajax(`${window.config.api}/commands`, `GET`)
				.then((commands) => {
					this.commands = commands;
					this.fillList(this.commands,
						document.querySelector('#command_items'),
						'command',
						this.cmdChanged.bind(this));
					resolve(commands);
				},
				(err) => {
					this.status(err);
					this.fillList(this.commands,
						document.querySelector('#command_items'),
						'command',
						this.cmdChanged.bind(this));
					reject(err);
				});
		});

		Promise.all([lp, cp]).then(() => {
			this.getStatus(true);
			setInterval(this.getStatus.bind(this), 5000);
		}, (err) => {
			this.getStatus(true);
			setInterval(this.getStatus.bind(this), 5000);
		});

		switch (window.location.hash) {
			case '#commands':
				document.querySelector('#tabc').checked = true;
				break;
			case '#layouts':
				document.querySelector('#tabl').checked = true;
				break;
		}
	}

	selectCommandFrame() {
		window.location.hash = '#commands';
	}

	findByKey(arr, key, value) {
		return arr.find((val) => {
			return val[key] === value;
		});
	}

	findIndexByKey(arr, key, value) {
		return arr.findIndex((val) => {
			return val[key] === value;
		});
	}

	selectLayoutFrame() {
		window.location.hash = '#layouts';
		if (this.state && this.state.layout) {
			let display = document.querySelector('#display_selector').value;
			let layout = this.findByKey(this.state.layout, 'display', display);
			if (layout) {
				let l = this.findByKey(this.layouts, 'display', display);
				let index = this.findIndexByKey(l.layouts, 'name', layout.layout);
				if (index >= 0) {
					let target = document.querySelector(`#layout_${index}`);
					target.checked = true;
					target.dispatchEvent(new Event('change'));
				}
			}
		}
	}
}

window.onload = () => {
	window.controller = new Controller();
};
