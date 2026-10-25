/* Universe RP site: the star field behind every page.
   Three layers of stars on one fixed canvas. The near layers drift a little when the page scrolls
   (parallax) and every star twinkles gently. The colours follow the mod's art: mostly white and
   blue-white, with a few faint red, gold and green ones.
   It draws one still frame (no twinkle, no drift) when the visitor asks for reduced motion, and it
   pauses while the tab is hidden. No libraries. */
(function () {
	"use strict";

	var canvas = document.getElementById("starfield");
	if (!canvas || !canvas.getContext) {
		return;
	}
	var ctx = canvas.getContext("2d");
	var reduceMotion = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

	// [density per 10,000 px², parallax factor, size range, brightness range]
	var LAYERS = [
		{ density: 1.1, parallax: 0.02, size: [0.35, 0.8], alpha: [0.25, 0.6] },
		{ density: 0.35, parallax: 0.06, size: [0.6, 1.2], alpha: [0.45, 0.85] },
		{ density: 0.07, parallax: 0.12, size: [1.0, 1.8], alpha: [0.7, 1.0] }
	];
	var TINTS = [
		[255, 255, 255], [255, 255, 255], [255, 255, 255], [225, 238, 255], [200, 222, 255], [190, 215, 255],
		[255, 214, 170], [255, 170, 160], [180, 255, 200], [255, 240, 200]
	];

	var stars = [];
	var width = 0, height = 0, dpr = 1;
	var scrollY = window.pageYOffset || 0;
	var running = false;
	var lastFrame = 0;
	var FRAME_MS = 1000 / 30;	// 30 fps is plenty for a twinkle, and kind to laptops

	function rand(a, b) {
		return a + Math.random() * (b - a);
	}

	function build() {
		dpr = Math.min(window.devicePixelRatio || 1, 2);
		width = window.innerWidth;
		height = window.innerHeight;
		canvas.width = Math.round(width * dpr);
		canvas.height = Math.round(height * dpr);
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

		stars = [];
		var area = width * height / 10000;
		for (var l = 0; l < LAYERS.length; l++) {
			var layer = LAYERS[l];
			var count = Math.round(area * layer.density);
			for (var i = 0; i < count; i++) {
				var tint = TINTS[(Math.random() * TINTS.length) | 0];
				stars.push({
					x: Math.random() * width,
					// spread over a taller band so parallax never runs out of stars
					y: Math.random() * height,
					r: rand(layer.size[0], layer.size[1]),
					a: rand(layer.alpha[0], layer.alpha[1]),
					p: layer.parallax,
					tw: rand(0.4, 1.6),			// twinkle speed
					ph: Math.random() * Math.PI * 2,	// twinkle phase
					c: "rgba(" + tint[0] + "," + tint[1] + "," + tint[2] + ","
				});
			}
		}
	}

	function draw(time) {
		ctx.clearRect(0, 0, width, height);
		var t = time / 1000;
		for (var i = 0; i < stars.length; i++) {
			var s = stars[i];
			var y = s.y - (reduceMotion ? 0 : scrollY * s.p);
			y = ((y % height) + height) % height;
			var alpha = s.a;
			if (!reduceMotion) {
				alpha *= 0.72 + 0.28 * Math.sin(t * s.tw + s.ph);
			}
			ctx.fillStyle = s.c + alpha.toFixed(3) + ")";
			ctx.beginPath();
			ctx.arc(s.x, y, s.r, 0, 6.2832);
			ctx.fill();
			if (s.r > 1.25) {
				// the brightest stars get a soft halo
				ctx.fillStyle = s.c + (alpha * 0.12).toFixed(3) + ")";
				ctx.beginPath();
				ctx.arc(s.x, y, s.r * 3.2, 0, 6.2832);
				ctx.fill();
			}
		}
	}

	function loop(time) {
		if (!running) {
			return;
		}
		if (time - lastFrame >= FRAME_MS) {
			lastFrame = time;
			draw(time);
		}
		window.requestAnimationFrame(loop);
	}

	function start() {
		if (reduceMotion) {
			draw(0);
			return;
		}
		if (!running) {
			running = true;
			window.requestAnimationFrame(loop);
		}
	}

	function stop() {
		running = false;
	}

	var resizeTimer = 0;
	window.addEventListener("resize", function () {
		window.clearTimeout(resizeTimer);
		resizeTimer = window.setTimeout(function () {
			// phones fire resize when the address bar slides; only rebuild on a real size change
			if (Math.abs(window.innerWidth - width) > 40 || Math.abs(window.innerHeight - height) > 120) {
				build();
				draw(performance.now());
			}
		}, 150);
	});
	window.addEventListener("scroll", function () {
		scrollY = window.pageYOffset || 0;
	}, { passive: true });
	document.addEventListener("visibilitychange", function () {
		if (document.hidden) {
			stop();
		} else {
			start();
		}
	});

	build();
	draw(0);
	start();
})();
