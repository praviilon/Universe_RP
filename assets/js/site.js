/* Universe RP site: header behaviour and the latest-release badge. No libraries. */
(function () {
	"use strict";

	var REPO = "praviilon/Universe_RP";

	// Header: transparent over the hero, frosted glass once the page scrolls; menu button on phones.
	var header = document.querySelector(".site-header");
	if (header) {
		var onScroll = function () {
			header.classList.toggle("scrolled", (window.pageYOffset || 0) > 24);
		};
		window.addEventListener("scroll", onScroll, { passive: true });
		onScroll();

		var toggle = header.querySelector(".menu-toggle");
		if (toggle) {
			toggle.addEventListener("click", function () {
				var open = header.classList.toggle("menu-open");
				toggle.setAttribute("aria-expanded", open ? "true" : "false");
			});
			header.querySelectorAll(".nav a").forEach(function (a) {
				a.addEventListener("click", function () {
					header.classList.remove("menu-open");
					toggle.setAttribute("aria-expanded", "false");
				});
			});
		}
	}

	// Footer year.
	document.querySelectorAll("[data-year]").forEach(function (el) {
		el.textContent = String(new Date().getFullYear());
	});

	// Latest release: version and date from the GitHub API, kept for the visit (sessionStorage) so
	// moving between pages asks only once. Anything that goes wrong leaves the badge's plain
	// fallback text and link in place.
	var badges = document.querySelectorAll("[data-release-badge]");
	if (!badges.length || !window.fetch) {
		return;
	}

	function show(rel) {
		var date = new Date(rel.published_at);
		var when = isNaN(date) ? "" : date.toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric" });
		badges.forEach(function (el) {
			var text = el.querySelector("[data-release-text]");
			var link = el.querySelector("a");
			if (text) {
				text.textContent = "Latest release " + rel.tag_name + (when ? " · " + when : "");
			}
			if (link && rel.html_url) {
				link.href = rel.html_url;
			}
			el.classList.add("loaded");
		});
	}

	var KEY = "urp-latest-release";
	try {
		var cached = JSON.parse(window.sessionStorage.getItem(KEY) || "null");
		if (cached && cached.tag_name) {
			show(cached);
			return;
		}
	} catch (e) { /* storage blocked: just ask the API */ }

	fetch("https://api.github.com/repos/" + REPO + "/releases/latest", { headers: { Accept: "application/vnd.github+json" } })
		.then(function (r) {
			if (!r.ok) {
				throw new Error(r.status);
			}
			return r.json();
		})
		.then(function (rel) {
			var slim = { tag_name: rel.tag_name, published_at: rel.published_at, html_url: rel.html_url };
			try {
				window.sessionStorage.setItem(KEY, JSON.stringify(slim));
			} catch (e) { /* ignore */ }
			show(slim);
		})
		.catch(function () { /* keep the fallback text */ });
})();
