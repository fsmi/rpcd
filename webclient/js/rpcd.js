const controller = {
	init: function() {
		let layout_items = document.querySelector('#layout_items');

		for (let i = 0; i < 10; i++) {
			let item = document.createElement('li');

			let input = document.createElement('input');
			input.value = i;
			input.type = 'radio';
			input.name = 'layouts';
			input.id = `layout_${i}`;

			item.appendChild(input);

			let label = document.createElement('label');
			label.setAttribute('for',`layout_${i}`);
			label.textContent = `Dummy ${i}`;

			item.appendChild(label);
			layout_items.appendChild(item);
		}
	}
};

window.onload = () => {
	controller.init();
};
