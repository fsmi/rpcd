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
							reject('Cannot connect to server.');
						default:
							reject(`${request.status}: ${request.statusText}`);
					}
				}
			};
			console.log('request:', url, method, JSON.stringify(payload));
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
				this.status('reset: success');
			},
			(err) => {
				this.status(`reset: error: ${err}`);
			});
	}

	stop() {
		this.ajax(`${window.config.api}/stop`, 'GET').then(
			() => {
				this.status('stop: success');
			},
			(err) => {
				this.status(`stop: error: ${err}`);
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
				button = this.createButton(`${prefix}_${i}_button`,
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
		this.status(`Apply layout: ${this.layouts[layout].name}`);

		this.ajax(`${window.config.api}/${this.layouts[layout].name}`, 'GET').then(
			() => {
				this.status('applyLayout: success');
				this.state.layout = layout;
			},
			(err) => {
				this.status(`applyLayout: error ${err}`);
			}
		);
	}

	startCommand(i) {
		let command = this.commands[i];
		this.status(`Start command: ${i}`);

		let args = [];

		let ret = command.args.every((val, i) => {
			let elem = document.querySelector(`#arg_${i}`);
			let arg = {};
			arg.name = val.name;
			switch(val.type) {
				case 'string':
					arg.value = elem.value;
					break;
				case 'enum':
					if (val.options.indexOf(elem.value) < 0) {
						this.status(`error enum value not found for ${val.name}.`);
						return false;
					} else {
						arg.value = elem.value;
					}
					break;
			}
			args.push(arg);
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
			this.status('success');
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
			let input = document.createElement('input');
			input.id = `arg_${i}`;
			input['data-option'] = option;
			switch(option.type) {
				case 'string':
					input.type = 'text';
					input.placeholder = option.hint;
					break;
				case 'enum':
					input.type = 'text';
					input.setAttribute('list', `arglist_${i}`);
					input.pattern = option.options.join('|');
					input.placeholder = ' ';

					let datalist = document.createElement('datalist');
					datalist.id = `arglist_${i}`;
					option.options.forEach((item) => {
						let o = document.createElement('option');
						o.value = item;
						datalist.appendChild(o);
					});
					li.appendChild(datalist);
			}
			li.appendChild(label);
			li.appendChild(input);

			cmd_args.appendChild(li);
		});
	}

	createDummyLayouts() {
		for (let i = 0; i < 10; i++) {
			this.layouts.push(
				{
					name: `Layout ${i}`,
					screens: [
						{id: 0, width: 1600, height: 1200},
						{id: 1, width: 1600, height: 1200}
					],
					frames: [
						{id: i+10, ul_x: 0, ul_y:0, w:300, h: 400, screen: 0},
						{id: i+20, ul_x: 300, ul_y: 0, w: 300, h: 400, screen: 0},
						{id: i+30, ul_x: 600, ul_y: 0, w: 1000, h: 400, screen: 0},
						{id: i+40, ul_x: 0, ul_y: 200, w: 1200, h: 800, screen: 0},
						{id: i+10, ul_x: 0, ul_y:0, w:300, h: 400, screen: 1},
						{id: i+20, ul_x: 300, ul_y: 0, w: 300, h: 400, screen: 1},
						{id: i+30, ul_x: 600, ul_y: 0, w: 1000, h: 400, screen: 1},
						{id: i+40, ul_x: 0, ul_y: 200, w: 1200, h: 800, screen: 1}

					]
				}
			);
		}
	}

	createDummyCommands() {
		for (let i = 0; i < 10; i++) {
			this.commands.push(
				{
					name: `Command ${i}`,
					args: [
						{name: 'arg1', type:'string', hint:'url'},
						{name: 'arg2', type:'enum', options:['val1', 'val2']}
					]
				}
			);
		}
	}
	getStatus() {
		this.ajax(`${window.config.api}/status`, 'GET')
			.then((state) => {
				console.log(state);
				//state.running = [0, 2];
				if (state.running && state.running.length > 0) {
					state.running.forEach((cmd) => {
						console.log(cmd);
						this.addCmdStopButton(cmd);
					});
				}
			},
			(err) => {
				this.status(`cannot get status: ${err}`);
				console.log(`cannot get status: ${err}`);
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
			this.createDummyLayouts();
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
			this.createDummyCommands();
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
