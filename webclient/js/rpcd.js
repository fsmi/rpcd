class Controller {
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
		});
	}

	applyLayout() {
		let layout = document.querySelector('input[name="layouts"]:checked').value;
		console.log(`Apply layout: ${layout}`);
	}

	commandStart() {
		let command = document.querySelector('input[name="commands":checked').value;
		console.log(`Start command: ${command}`);
	}

	layoutChanged(event) {
		console.log(event.target.value);
	}

	cmdChanged(event) {
		console.log(event.target.value);

		let cmd = this.commands[event.target.value];

		document.querySelector('#cmdHeadline').textContent = cmd.name;

		let cmd_args = document.querySelector('#cmd_args');
		cmd_args.innerHTML = '';

		cmd.args.forEach((option, i) => {
			let li = document.createElement('li');

			let label = document.createElement('label');
			label.setAttribute('for', `arg_${i}`);
			label.textContent = `${option.name}:`;
			let input = document.createElement('input');
			switch(option.type) {
				case 'string':
					input.id = 'arg_${i}';
					input.type = 'text';
					input['data-option'] = option;
					break;
				case 'enum':
					input.id = `arg_${i}`;
					input.type = 'text';
					input.setAttribute('list', `arglist_${i}`);
					input['data-option'] = option;

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

	init() {
		this.layouts = [];
		this.commands = [];
		// dummies
		for (let i = 0; i < 10; i++) {
			this.layouts.push(
				{name: `Layout ${i}`}
			);
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

		this.fillList(this.layouts, document.querySelector('#layout_items'), 'layout', this.layoutChanged.bind(this));
		this.fillList(this.commands, document.querySelector('#command_items'), 'command', this.cmdChanged.bind(this));
	}
}

window.onload = () => {
	window.controller = new Controller();
	window.controller.init();
};
