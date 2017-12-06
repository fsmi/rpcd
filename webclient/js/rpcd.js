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
							reject(`${request.statusCode}: ${request.statusText}`);
					}
				}
			};
			console.log('request:', url, method, payload);
			request.open(method, url);
			request.send(payload);
		});
	}

	reset() {
		this.ajax(`${window.config.api}/reset`, 'GET').then(
			() => {
				console.log('reset: success');
			},
			(err) => {
				console.log(`reset: error: ${err}`);
			});
	}

	stop() {
		this.ajax(`${window.config.api}/stop`, 'GET').then(
			() => {
				console.log('stop: success');
			},
			(err) => {
				console.log(`stop: error: ${err}`);
			});
	}

	fillList(list, elem, prefix, changeListener) {
		list.forEach((item, i) => {
			let li = document.createElement('li');

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
			elem.appendChild(li);
			if (i === 0) {
				input.checked = 'true';
				input.dispatchEvent(new Event('change'));
			}
		});
	}

	applyLayout() {
		let layout = document.querySelector('input[name="layouts"]:checked').value;
		console.log(`Apply layout: ${this.layouts[layout].name}`);

		this.ajax(`${window.config.api}/${this.layouts[layout].name}`, 'GET').then(
			() => {
				console.log('success');
				this.state.layout = layout;
			},
			(err) => {
				console.log('error', err);
			}
		);
	}

	startCommand() {
		let command_id = document.querySelector('input[name="commands"]:checked').value;
		let command = this.commands[command_id];
		console.log(`Start command: ${command_id}`);

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
						console.log(`error enum value not found for ${val.name}.`);
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
			console.log('success');
		},
		(err) => {
			console.log(err);
		});
	}

	layoutChanged(event) {

		let layout = this.layouts[event.target.value];
		let canvas = document.querySelector('#preview');
		let ctx = canvas.getContext('2d');
		canvas.width = layout.xres || 800;
		canvas.height = layout.yres || 600;

		ctx.lineWidth = 4;
		ctx.strokeStyle = '#000000';
		ctx.font = '10em Georgia';
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';

		layout.frames = layout.frames || [];
		layout.frames.forEach((frame) => {
			ctx.rect(frame.ul_x, frame.ul_y, frame.lr_x - frame.ul_x, frame.lr_y - frame.ul_y);
			ctx.stroke();
			ctx.fillText(`${frame.id}`,
				frame.ul_x + ((frame.lr_x - frame.ul_x)/2),
				frame.ul_y + ((frame.lr_y - frame.ul_y)/2));
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
					xres: 1600,
					yres: 1200,
					frames: [
						{id: i+10, ul_x: 0, ul_y:0, lr_x:300, lr_y: 400},
						{id: i+20, ul_x: 300, ul_y: 0, lr_x: 600, lr_y: 400},
						{id: i+30, ul_x: 600, ul_y: 0, lr_x: 1600, lr_y: 400},
						{id: i+40, ul_x: 0, ul_y: 400, lr_x: 1600, lr_y: 1200}
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

	constructor() {
		this.layouts = [];
		this.commands = [];
		this.state = {
			layout: 0
		};
		// dummies
		this.ajax(`${window.config.api}/layouts`, 'GET').then((layouts) => {
			this.layouts = layouts;
			this.fillList(this.layouts,
				document.querySelector('#layout_items'),
				'layout',
				this.layoutChanged.bind(this));
		},
		(err) => {
			console.log(err);
			this.createDummyLayouts();
			this.fillList(this.layouts,
				document.querySelector('#layout_items'),
				'layout',
				this.layoutChanged.bind(this));
		});
		this.ajax(`${window.config.api}/commands`, `GET`).then((commands) => {
			this.commands = commands;
			this.fillList(this.commands,
				document.querySelector('#command_items'),
				'command',
				this.cmdChanged.bind(this));
		},
		(err) => {
			console.log(err);
			this.createDummyCommands();
			this.fillList(this.commands,
				document.querySelector('#command_items'),
				'command',
				this.cmdChanged.bind(this));
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
