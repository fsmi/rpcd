function ajax(url, method, payload) {
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
					default:
						reject(request.statusText);
				}
			}
		};

		request.open(method, url);
		request.send(payload);
	});
}
