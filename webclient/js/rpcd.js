class Controller {
	ajax(url, method, payload) {
		return new Promise(function(resolve, reject) {
			var request = new XMLHttpRequest();
			request.onreadystatechange = function() {
				if (request.readyState === XMLHttpRequest.DONE) {
					switch(request.status) {
						case 200:
							document.querySelector('#status-box').classList.remove('api-error');
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
							document.querySelector('#status-box').classList.add('api-error');
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
		this.status(`Applied layout ${this.layouts[layout].name}`);

		this.ajax(`${window.config.api}/layout/${this.layouts[layout].name}`, 'GET').then(
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
		if (command.windows && command.windows > 0) {
			options.fullscreen = document.querySelector('#fullscreen').checked ? 1 : 0;
			options.frame = parseInt(document.querySelector('#cmdFrame').value, 10);
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
		console.log(this.state.layout);
		let layout = this.layouts.find((l) => {
			return l.name === this.state.layout;
		});

		if (!layout) {
			return;
		}

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

	getStatus() {
		this.ajax(`${window.config.api}/status`, 'GET')
			.then((state) => {
				console.log(state);
				let oldstate = this.state;
				this.state = state;
				if (oldstate.layout !== this.state.layout) {
					this.fillFrameBox();
				}
				if (state.running) {
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
							label.classList.add('running');
							elemStart.classList.add('hidden');
							elemStop.classList.remove('hidden');
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
			layout: "",
			running: [0]
		};

		if (window.config.api.startsWith('https:')) {
			document.querySelector('#status-box').classList.add('api-error');
			this.status('API not accessible over HTTPS');
			return;
		}

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
			setInterval(this.getStatus.bind(this), 5000);
		}, () => {
			this.getStatus();
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
}

window.onload = () => {
	window.controller = new Controller();
};
