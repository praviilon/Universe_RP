/* Universe RP site: the download picker and the "all downloads" tables.
   The visitor picks what he wants to do (play or host), his system and its architecture; the picker
   preselects all three from the browser and remembers the choice on this device. It then links the
   matching TaystJK archive and the matching Universe RP package.
   Every link is a plain GitHub "latest release" download URL, so it works without the API. The API
   is only asked (once per visit) for the version, date and file sizes; if it fails, nothing breaks. */
(function () {
	"use strict";

	var OUR_REPO = "praviilon/Universe_RP";
	var TJK_REPO = "taysta/TaystJK";
	var OUR_DL = "https://github.com/" + OUR_REPO + "/releases/latest/download/";
	var TJK_DL = "https://github.com/" + TJK_REPO + "/releases/latest/download/";
	var TJK_INSTALL = "https://taysta.github.io/TaystJK/install/";
	var TJK_COMPILE = "https://taysta.github.io/TaystJK/development/compiling/";

	var SYSTEMS = {
		windows: {
			label: "Windows",
			ext: "zip",
			archs: [["x86_64", "64-bit"], ["x86", "32-bit"]],
			tjk: { x86_64: "TaystJK-windows-x86_64.zip", x86: "TaystJK-windows-x86.zip" },
			play: "universe_rp_play.bat",
			host: "universe_rp_host.bat"
		},
		linux: {
			label: "Linux",
			ext: "tar.gz",
			archs: [["x86_64", "64-bit"], ["x86", "32-bit"], ["arm64", "ARM64"]],
			tjk: { x86_64: "TaystJK-linux-x86_64.tar.gz", x86: "TaystJK-linux-x86.tar.gz", arm64: null },
			play: "universe_rp_play.sh",
			host: "universe_rp_host.sh"
		},
		macos: {
			label: "macOS",
			ext: "tar.gz",
			archs: [["arm64", "Apple Silicon"], ["x86_64", "Intel"]],
			// the universal2 build runs natively on both, and the launcher finds taystjk.app in it
			tjk: { arm64: "TaystJK-macos-universal2.tar.gz", x86_64: "TaystJK-macos-universal2.tar.gz" },
			play: "universe_rp_play.command",
			host: "universe_rp_host.command"
		}
	};

	function ourName(os, arch, purpose) {
		return "UniverseRP-" + os + "-" + arch + "-Release-" + (purpose === "host" ? "Full" : "Client") + "." + SYSTEMS[os].ext;
	}

	// ---------- GitHub API: sizes, version and date (optional) ----------

	var sizes = {};
	var ourRelease = null;
	var listeners = [];

	function fetchRelease(repo, key) {
		var cached = null;
		try {
			cached = JSON.parse(window.sessionStorage.getItem(key) || "null");
		} catch (e) { /* storage blocked */ }
		if (cached) {
			return Promise.resolve(cached);
		}
		if (!window.fetch) {
			return Promise.reject(new Error("no fetch"));
		}
		return fetch("https://api.github.com/repos/" + repo + "/releases/latest", { headers: { Accept: "application/vnd.github+json" } })
			.then(function (r) {
				if (!r.ok) {
					throw new Error(r.status);
				}
				return r.json();
			})
			.then(function (rel) {
				var slim = {
					tag_name: rel.tag_name,
					published_at: rel.published_at,
					html_url: rel.html_url,
					assets: (rel.assets || []).map(function (a) { return [a.name, a.size]; })
				};
				try {
					window.sessionStorage.setItem(key, JSON.stringify(slim));
				} catch (e) { /* ignore */ }
				return slim;
			});
	}

	function addSizes(rel) {
		(rel.assets || []).forEach(function (a) {
			sizes[a[0]] = a[1];
		});
	}

	function formatSize(bytes) {
		return bytes ? (bytes / 1048576).toFixed(1) + " MB" : "";
	}

	function formatDate(iso) {
		var d = new Date(iso);
		return isNaN(d) ? "" : d.toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric" });
	}

	function refresh() {
		listeners.forEach(function (fn) { fn(); });
		document.querySelectorAll("[data-size]").forEach(function (el) {
			var s = sizes[el.getAttribute("data-size")];
			if (s) {
				el.textContent = formatSize(s);
			}
		});
	}

	fetchRelease(OUR_REPO, "urp-release-full").then(function (rel) {
		ourRelease = rel;
		addSizes(rel);
		refresh();
	}).catch(function () { /* links still work */ });
	fetchRelease(TJK_REPO, "urp-tjk-release-full").then(function (rel) {
		addSizes(rel);
		refresh();
	}).catch(function () { /* links still work */ });

	// ---------- Detection ----------

	function detect() {
		var ua = navigator.userAgent || "";
		var result = { os: "windows", arch: "x86_64", mobile: /Android|iPhone|iPad|iPod/i.test(ua) };
		if (/Mac OS X|Macintosh/i.test(ua) && !/iPhone|iPad|iPod/i.test(ua)) {
			result.os = "macos";
			result.arch = "arm64";	// most Macs since 2020; Chromium tells us below
		} else if (/Linux|X11|CrOS/i.test(ua) && !/Android/i.test(ua)) {
			result.os = "linux";
			if (/aarch64|arm64|armv8/i.test(ua)) {
				result.arch = "arm64";
			} else if (/i[3-6]86/i.test(ua) && !/x86_64|amd64/i.test(ua)) {
				result.arch = "x86";
			}
		} else if (/Windows/i.test(ua)) {
			// 32-bit Windows says so in the user agent; 64-bit and ARM Windows both take the 64-bit build
			if (!/Win64|WOW64|x64|amd64|arm64/i.test(ua)) {
				result.arch = "x86";
			}
		}
		return result;
	}

	function refineArch(choice, done) {
		var uad = navigator.userAgentData;
		if (!uad || !uad.getHighEntropyValues) {
			done(choice);
			return;
		}
		uad.getHighEntropyValues(["architecture", "bitness"]).then(function (v) {
			if (choice.os === "macos") {
				choice.arch = v.architecture === "x86" ? "x86_64" : "arm64";
			} else if (choice.os === "linux") {
				choice.arch = v.architecture === "arm" ? "arm64" : (v.bitness === "32" ? "x86" : "x86_64");
			} else if (choice.os === "windows") {
				choice.arch = (v.architecture === "x86" && v.bitness === "32") ? "x86" : "x86_64";
			}
			done(choice);
		}).catch(function () {
			done(choice);
		});
	}

	// ---------- The picker ----------

	var STORE = "urp-download-choice";

	function loadChoice() {
		try {
			var c = JSON.parse(window.localStorage.getItem(STORE) || "null");
			if (c && SYSTEMS[c.os] && (c.purpose === "play" || c.purpose === "host")) {
				return c;
			}
		} catch (e) { /* ignore */ }
		return null;
	}

	function saveChoice(c) {
		try {
			window.localStorage.setItem(STORE, JSON.stringify(c));
		} catch (e) { /* ignore */ }
	}

	function el(tag, attrs, children) {
		var e = document.createElement(tag);
		Object.keys(attrs || {}).forEach(function (k) {
			if (k === "text") {
				e.textContent = attrs[k];
			} else if (k === "html") {
				e.innerHTML = attrs[k];
			} else {
				e.setAttribute(k, attrs[k]);
			}
		});
		(children || []).forEach(function (c) {
			if (c) {
				e.appendChild(c);
			}
		});
		return e;
	}

	var uid = 0;

	function radioGroup(name, legend, options, current, onChange) {
		uid++;
		var fs = el("fieldset", { "class": "seg-group" }, [el("legend", { text: legend })]);
		var row = el("div", { "class": "seg" });
		options.forEach(function (opt) {
			var id = "pick-" + name + "-" + opt[0] + "-" + uid;
			var input = el("input", { type: "radio", name: name + "-" + uid, id: id, value: opt[0] });
			if (opt[0] === current) {
				input.checked = true;
			}
			input.addEventListener("change", function () {
				if (input.checked) {
					onChange(opt[0]);
				}
			});
			var label = el("label", { "for": id }, [el("span", { "class": "seg-title", text: opt[1] })]);
			if (opt[2]) {
				label.appendChild(el("span", { "class": "seg-note", text: opt[2] }));
			}
			row.appendChild(input);
			row.appendChild(label);
		});
		fs.appendChild(row);
		return fs;
	}

	function downloadCard(step, title, name, href, note) {
		var card = el("div", { "class": "dl-card" });
		card.appendChild(el("p", { "class": "dl-step", text: step }));
		card.appendChild(el("h3", { text: title }));
		if (name) {
			var a = el("a", { "class": "btn btn-primary dl-btn", href: href });
			a.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12m0 0l-5-5m5 5l5-5M4 19h16" stroke-linecap="round" stroke-linejoin="round"/></svg>Download';
			card.appendChild(a);
			var meta = el("p", { "class": "dl-meta" }, [el("code", { text: name })]);
			var size = sizes[name] ? formatSize(sizes[name]) : "";
			if (size) {
				meta.appendChild(el("span", { text: " · " + size }));
			}
			card.appendChild(meta);
		}
		if (note) {
			card.appendChild(el("p", { "class": "dl-note", html: note }));
		}
		return card;
	}

	function buildPicker(root) {
		var compact = root.hasAttribute("data-compact");
		var state = loadChoice();
		var fromStore = !!state;
		if (!state) {
			var d = detect();
			state = { purpose: "play", os: d.os, arch: d.arch };
		}
		var mobile = detect().mobile;
		if (/(^|#)host\b/.test(window.location.hash)) {
			state.purpose = "host";
		}

		var form = el("div", { "class": "picker-form" });
		var result = el("div", { "class": "picker-result", "aria-live": "polite" });
		root.innerHTML = "";
		root.appendChild(form);
		root.appendChild(result);

		function archValid() {
			return SYSTEMS[state.os].archs.some(function (a) { return a[0] === state.arch; });
		}

		function renderForm() {
			form.innerHTML = "";
			form.appendChild(radioGroup("purpose", "I want to", [
				["play", "Play", "Client package: join servers"],
				["host", "Host a server", "Full package: run your own"]
			], state.purpose, function (v) { state.purpose = v; update(); }));
			form.appendChild(radioGroup("os", "System", [
				["windows", "Windows"], ["linux", "Linux"], ["macos", "macOS"]
			], state.os, function (v) {
				state.os = v;
				if (!archValid()) {
					state.arch = SYSTEMS[v].archs[0][0];
				}
				renderForm();
				update();
			}));
			form.appendChild(radioGroup("arch", "Architecture", SYSTEMS[state.os].archs, state.arch, function (v) {
				state.arch = v;
				update();
			}));
			if (mobile && !fromStore) {
				form.appendChild(el("p", { "class": "dl-note", text: "On a phone or tablet? Pick the system of the computer you will play on." }));
			}
		}

		function update() {
			saveChoice(state);
			var sys = SYSTEMS[state.os];
			var host = state.purpose === "host";
			var tjk = sys.tjk[state.arch];
			var name = ourName(state.os, state.arch, state.purpose);
			result.innerHTML = "";

			var cards = el("div", { "class": "dl-cards" });
			if (tjk) {
				cards.appendChild(downloadCard("Step 1", "TaystJK engine", tjk, TJK_DL + tjk,
					"The game engine Universe RP runs on" + (host ? ", with its dedicated server" : "") + ". Already installed? Skip this. <a href=\"" + TJK_INSTALL + "\">TaystJK install guide</a>"));
			} else {
				cards.appendChild(downloadCard("Step 1", "TaystJK engine", null, null,
					"TaystJK publishes no Linux arm64 build: compile it yourself with the <a href=\"" + TJK_COMPILE + "\">TaystJK build guide</a>. The launcher looks for <code>taystjk.arm64</code>" + (host ? " and <code>taystjkded.arm64</code>" : "") + "."));
			}
			cards.appendChild(downloadCard("Step 2", "Universe RP " + (host ? "Full package" : "Client package"), name, OUR_DL + name,
				host ? "Everything a server needs: the server module, <code>universe_rp_server.cfg</code>, the host scripts and the data folders. It plays as well." : "Everything you need to join a Universe RP server."));
			result.appendChild(cards);

			if (!compact) {
				var steps = el("ol", { "class": "dl-steps" });
				steps.appendChild(el("li", { html: "Extract TaystJK into your Jedi Academy <code>GameData</code> folder, in place of the OpenJK executables (or into a separate folder with a copy of <code>base</code>)." }));
				steps.appendChild(el("li", { html: "Extract the Universe RP package into that same folder, so <code>UniverseRP</code> sits next to the TaystJK executables." }));
				if (host) {
					steps.appendChild(el("li", { html: "Set the server name, <code>rconpassword</code> and your settings in the fresh <code>UniverseRP/universe_rp_server.cfg</code>, then run <code>UniverseRP/" + sys.host + "</code>." }));
				} else {
					steps.appendChild(el("li", { html: "Launch the game with <code>UniverseRP/" + sys.play + "</code>: press Enter to play, or paste a server's IP to connect." }));
				}
				result.appendChild(el("div", { "class": "dl-next" }, [
					el("h3", { text: "Then" }), steps,
					el("p", { html: host ? "<a href=\"host.html\">Full server guide</a>" : "<a href=\"play.html\">Full player guide</a>" })
				]));
			}

			var foot = el("p", { "class": "dl-release" });
			if (ourRelease) {
				foot.innerHTML = "Universe RP " + ourRelease.tag_name + (ourRelease.published_at ? " · " + formatDate(ourRelease.published_at) : "") +
					" · <a href=\"" + ourRelease.html_url + "\">release notes</a>";
			} else {
				foot.innerHTML = "Always the latest release · <a href=\"https://github.com/" + OUR_REPO + "/releases/latest\">release notes</a>";
			}
			if (compact) {
				foot.innerHTML += " · <a href=\"download.html\">all downloads and steps</a>";
			}
			result.appendChild(foot);
		}

		listeners.push(update);
		renderForm();
		update();
		if (!fromStore) {
			refineArch(state, function () {
				renderForm();
				update();
			});
		}
	}

	document.querySelectorAll("[data-download-picker]").forEach(buildPicker);
	refresh();
})();
