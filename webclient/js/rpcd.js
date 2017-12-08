class Controller {
	ajax(url, method, payload) {
		return new Promise(function(resolve, reject) {
			var request = new XMLHttpRequest();
			request.onreadystatechange = function() {
				if (request.readyState === XMLHttpRequest.DONE) {
					switch(request.status) {
						case 200:
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
							reject('Failed to access API');
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
			},
			(err) => {
				this.status(`Reset encountered an error: ${err}`);
			});
	}

	stopCommand(i) {
		this.ajax(`${window.config.api}/stop/${this.commands[i].name}`, 'GET').then(
			() => {
				this.status('Command stopped');
			},
			(err) => {
				this.status(`Failed to stop command: ${err}`);
			});
	}

	fillList(list, elem, prefix, changeListener) {
		list.forEach((item, i) => {
			let li = document.createElement('li');
			li.id = `li_${prefix}_${i}`;
			let input = document.createElement('input');
			input.value = i;
			input.type = 'radio';
			input.name = `${prefix}s`;
			input.id = `${prefix}_${i}`;

			input.addEventListener('change', changeListener);

			li.appendChild(input);

			let label = document.createElement('label');
			label.setAttribute('for',`${prefix}_${i}`);
			label.textContent = item.name;
			let button;

			if (prefix === 'command') {
				button = this.createButton(`${prefix}_${i}_stop`,
					'stop', () => {
						this.stopCommand(i);
					});
				button.classList.add('hidden');
				li.appendChild(button);
				button = this.createButton(`${prefix}_${i}_start`,
					'start', () => {
						this.startCommand(i);
					});
			} else {
				button = this.createButton(`${prefix}_${i}_button`,
					'apply', this.applyLayout.bind(this));
			}

			li.appendChild(label);
			li.appendChild(button);
			elem.appendChild(li);
			if (i === 0) {
				input.checked = 'true';
				input.dispatchEvent(new Event('change'));
			}
		});
	}

	createButton(id, text, listener) {
		let span = document.createElement('span');
		span.classList.add('stopButton');
		span.id = id;
		span.textContent = text;
		span.addEventListener('click', listener);
		return span;
	}

	applyLayout() {
		let layout = document.querySelector('input[name="layouts"]:checked').value;
		this.status(`Applied layout ${this.layouts[layout].name}`);

		this.ajax(`${window.config.api}/layout/${this.layouts[layout].name}`, 'GET').then(
			() => {
				this.status('Layout loaded successfully');
				this.state.layout = layout;
			},
			(err) => {
				this.status(`Failed to apply layout: ${err}`);
			}
		);
	}

	startCommand(i) {
		let command = this.commands[i];
		this.status(`Start command: ${i}`);

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
			fullscreen: document.querySelector('#fullscreen').checked ? 1 : 0,
			frame: parseInt(document.querySelector('#cmdFrame').value, 10),
			arguments: args
		};

		this.ajax(`${window.config.api}/command/${command.name}`, 'POST', options).then(
		(ans) => {
			this.status('Command started');
		},
		(err) => {
			this.status(err);
		});
	}

	layoutChanged(event) {

		let layout = this.layouts[event.target.value];
		let canvas = document.querySelector('#previews');
		canvas.innerHTML = '';
		let screens = {};

		layout.screens = layout.screens || [];
		layout.screens.forEach((screen) => {
			let div = document.createElement('div');
			let c = document.createElement('canvas');
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
			console.log(`draw frame: ${frame.id} on screen ${frame.screen}`);
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
		box.innerHTML = '';
		let layout = this.layouts[this.state.layout];

		layout.frames.forEach((frame, i) => {
			let option = document.createElement('option');
			option.value = i;
			option.textContent = frame.id;
			box.appendChild(option);
		});
	}

	cmdChanged(event) {
		let cmd = this.commands[event.target.value];

		document.querySelector('#cmdHeadline').textContent = cmd.name;
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

	getStatus() {
		this.ajax(`${window.config.api}/status`, 'GET')
			.then((state) => {
				console.log(state);
				if (state.running && state.running.length > 0) {
					this.commands.forEach((elem) => {
						let id = state.running.findIndex((val) => {
							return val === elem.name;
						});

						let elemStart = document.querySelector(`#command_${id}_start`);
						let elemStop = document.querySelector(`#command_${id}_stop`);
						if (id) {
							elemStart.classList.add('hidden');
							elemStop.classList.remove('hidden');
						} else {
							elemStop.classList.remove('hidden');
							elemStart.classList.add('hidden');
						}
					});
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
			layout: 0,
			running: [0]
		};
		// dummies
		let lp = this.ajax(`${window.config.api}/layouts`, 'GET');
		lp.then((layouts) => {
			this.layouts = layouts;
			this.fillList(this.layouts,
				document.querySelector('#layout_items'),
				'layout',
				this.layoutChanged.bind(this));
		},
		(err) => {
			this.status(err);
			this.fillList(this.layouts,
				document.querySelector('#layout_items'),
				'layout',
				this.layoutChanged.bind(this));
		});
		let cp = this.ajax(`${window.config.api}/commands`, `GET`);
		cp.then((commands) => {
			this.commands = commands;
			this.fillList(this.commands,
				document.querySelector('#command_items'),
				'command',
				this.cmdChanged.bind(this));
		},
		(err) => {
			this.status(err);
			this.fillList(this.commands,
				document.querySelector('#command_items'),
				'command',
				this.cmdChanged.bind(this));
		});

		Promise.all(lp, cp).then(() => {
			this.getStatus();
		}, () => {
			this.getStatus();
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
}

window.onload = () => {
	window.controller = new Controller();
};
