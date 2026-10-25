/* Universe RP site: the gallery's full-size viewer. No libraries. Without it every thumbnail is a plain
   link to its full-size image, so the page works without JavaScript too. */
(function () {
	"use strict";

	var links = Array.prototype.slice.call(document.querySelectorAll("[data-gallery] a.shot"));
	if (!links.length) {
		return;
	}

	var ICON = {
		close: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18"/></svg>',
		prev: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7"/></svg>',
		next: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 5l7 7-7 7"/></svg>'
	};

	function button(cls, label, svg) {
		var b = document.createElement("button");
		b.type = "button";
		b.className = "lb-btn " + cls;
		b.setAttribute("aria-label", label);
		b.innerHTML = svg;
		return b;
	}

	// the viewer: built once, hidden until a thumbnail is chosen
	var box = document.createElement("div");
	box.className = "lightbox";
	box.hidden = true;
	box.setAttribute("role", "dialog");
	box.setAttribute("aria-modal", "true");
	box.setAttribute("aria-label", "Screenshot viewer");

	var stage = document.createElement("div");
	stage.className = "lb-stage";
	var img = document.createElement("img");
	img.decoding = "async";
	stage.appendChild(img);

	var bar = document.createElement("div");
	bar.className = "lb-bar";
	var group = document.createElement("span");
	group.className = "lb-group";
	var count = document.createElement("span");
	count.setAttribute("aria-live", "polite");
	bar.appendChild(group);
	bar.appendChild(count);

	var btnClose = button("lb-close", "Close", ICON.close);
	var btnPrev = button("lb-prev", "Previous screenshot", ICON.prev);
	var btnNext = button("lb-next", "Next screenshot", ICON.next);

	box.appendChild(stage);
	box.appendChild(bar);
	box.appendChild(btnPrev);
	box.appendChild(btnNext);
	box.appendChild(btnClose);
	document.body.appendChild(box);

	var current = -1;
	var lastFocus = null;

	function thumbOf(i) {
		var t = links[i].querySelector("img");
		return t ? t : null;
	}

	function preload(i) {
		var n = (i + links.length) % links.length;
		var p = new Image();
		p.src = links[n].href;
	}

	function show(i) {
		current = (i + links.length) % links.length;
		var link = links[current];
		var thumb = thumbOf(current);
		var full = link.href;

		// the thumbnail at once, the full image when it has loaded (if it is still the one shown)
		img.alt = thumb ? thumb.alt : "";
		img.src = thumb ? thumb.currentSrc || thumb.src : full;
		var loader = new Image();
		loader.onload = function () {
			if (links[current] === link) {
				img.src = full;
			}
		};
		loader.src = full;

		group.textContent = link.getAttribute("data-group") || "";
		count.textContent = (current + 1) + " / " + links.length;
		preload(current + 1);
		preload(current - 1);
	}

	function open(i) {
		lastFocus = document.activeElement;
		show(i);
		box.hidden = false;
		document.body.classList.add("lb-open");
		btnClose.focus();
	}

	function close() {
		box.hidden = true;
		document.body.classList.remove("lb-open");
		current = -1;
		if (lastFocus && lastFocus.focus) {
			lastFocus.focus();
		}
	}

	links.forEach(function (a, i) {
		a.addEventListener("click", function (e) {
			// a new tab or window (Ctrl, Cmd, Shift, middle button) gets the image itself
			if (e.ctrlKey || e.metaKey || e.shiftKey || e.altKey || e.button !== 0) {
				return;
			}
			e.preventDefault();
			open(i);
		});
	});

	btnClose.addEventListener("click", close);
	btnPrev.addEventListener("click", function () { show(current - 1); });
	btnNext.addEventListener("click", function () { show(current + 1); });

	// a click on the dark space around the image closes it
	box.addEventListener("click", function (e) {
		if (e.target === box || e.target === stage || e.target === bar) {
			close();
		}
	});

	document.addEventListener("keydown", function (e) {
		if (box.hidden) {
			return;
		}
		if (e.key === "Escape") {
			e.preventDefault();
			close();
		} else if (e.key === "ArrowLeft") {
			e.preventDefault();
			show(current - 1);
		} else if (e.key === "ArrowRight") {
			e.preventDefault();
			show(current + 1);
		} else if (e.key === "Tab") {
			// focus stays on the viewer's three buttons
			var order = [btnPrev, btnNext, btnClose];
			var at = order.indexOf(document.activeElement);
			e.preventDefault();
			order[(at + (e.shiftKey ? order.length - 1 : 1) + order.length) % order.length].focus();
		}
	});

	// swipe left or right on a touch screen
	var touchX = null;
	var touchY = null;
	box.addEventListener("touchstart", function (e) {
		if (e.touches.length === 1) {
			touchX = e.touches[0].clientX;
			touchY = e.touches[0].clientY;
		}
	}, { passive: true });
	box.addEventListener("touchend", function (e) {
		if (touchX === null || !e.changedTouches.length) {
			return;
		}
		var dx = e.changedTouches[0].clientX - touchX;
		var dy = e.changedTouches[0].clientY - touchY;
		touchX = touchY = null;
		if (Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(dy)) {
			show(current + (dx < 0 ? 1 : -1));
		}
	}, { passive: true });
})();
