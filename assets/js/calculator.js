/* Universe RP site: the cvar calculator. The same sections and bits as universe_rp_calculator.html in
   the Full package: tick what you want and copy the value, or paste a value to see what it means.
   Bits the page does not list (or hides) are kept as they are when you tick boxes. */
(function () {
	"use strict";

	var SECTIONS = [{"cvar": "g_weaponDisable", "bits": [[0, "None", true, ""], [1, "Stun Baton", false, ""], [2, "Melee", false, ""], [3, "Lightsaber", false, ""], [4, "Heavy Blaster Pistol", false, ""], [5, "Blaster Rifle", false, ""], [6, "Disruptor Rifle", false, ""], [7, "Bowcaster", false, ""], [8, "Heavy Repeater", false, ""], [9, "DEMP 2", false, ""], [10, "Flechette", false, ""], [11, "Merr Sonn", false, ""], [12, "Thermal Detonators", false, ""], [13, "Trip Mines", false, ""], [14, "Detonation Packs", false, ""], [15, "Concussion Rifle", false, ""], [16, "Blaster Pistol", false, ""], [17, "Emplaced Gun", true, ""], [18, "Turret Gun", true, ""]], "desc": "Weapons removed from the map. <code>g_duelWeaponDisable</code> (Duel gametypes) takes the same values.", "presets": [["Saber only", 524279], ["Saber and melee", 524275]]}, {"cvar": "g_forcePowerDisable", "bits": [[0, "Heal", false, ""], [1, "Jump", false, ""], [2, "Speed", false, ""], [3, "Push", false, ""], [4, "Pull", false, ""], [5, "Mind Trick", false, ""], [6, "Grip", false, ""], [7, "Lightning", false, ""], [8, "Rage", false, ""], [9, "Protect", false, ""], [10, "Absorb", false, ""], [11, "Team Heal", false, ""], [12, "Team Energize", false, ""], [13, "Drain", false, ""], [14, "Seeing", false, ""], [15, "Saber Attack", false, ""], [16, "Saber Defend", false, ""], [17, "Saber Throw", false, ""]], "desc": "Force powers disabled on the server. <code>g_duelForcePowerDisable</code> (Duel gametypes) takes the same values.", "presets": []}, {"cvar": "g_userinfoValidate", "bits": [[0, "cl_guid", false, ""], [1, "cl_punkbuster", false, ""], [2, "ip", false, ""], [3, "name", false, ""], [4, "rate", false, ""], [5, "snaps", false, ""], [6, "model", false, ""], [7, "forcepowers", false, ""], [8, "color1", false, ""], [9, "color2", false, ""], [10, "handicap", false, ""], [11, "sex", false, ""], [12, "cg_predictItems", false, ""], [13, "saber1", false, ""], [14, "saber2", false, ""], [15, "char_color_red", false, ""], [16, "char_color_green", false, ""], [17, "char_color_blue", false, ""], [18, "teamtask", false, ""], [19, "password", false, ""], [20, "teamoverlay", false, ""], [21, "Size", false, ""], [22, "# of slashes", false, ""], [23, "Extended ascii", false, ""], [24, "Control chars (incl. tab)", false, ""], [25, "cp_sbRGB1", false, ""], [26, "cp_sbRGB2", false, ""], [27, "rpmod_client", false, ""], [28, "ja_guid", false, ""]], "desc": "Which parts of a player's userinfo the server checks against exploits.", "presets": [["Config default", 528482303]]}, {"cvar": "g_allowVote", "bits": [[0, "capturelimit", false, "always refused"], [1, "clientkick", false, ""], [2, "fraglimit", false, "always refused"], [3, "warm up", false, ""], [4, "gametype", false, ""], [5, "kick player", false, ""], [6, "map", false, ""], [7, "map restart", false, ""], [8, "nextmap", false, ""], [9, "poll", false, ""], [10, "timelimit", false, "always refused"]], "desc": "Which votes players may call. <code>-1</code> allows every vote, <code>0</code> none.", "presets": [["Config default", 976]]}, {"cvar": "dmflags", "bits": [[3, "No Falling Damage (8)", false, ""], [4, "Fixed 80 FOV (16)", false, ""], [5, "No Footsteps (32)", false, ""], [6, "No Drowning (64)", false, ""]], "desc": "Game rule flags.", "presets": []}, {"cvar": "rp_default_account_permissions", "bits": [[0, "NPC management", false, ""], [1, "No Clip", false, ""], [2, "Give Admin", false, ""], [3, "Teleport", false, ""], [4, "Admin Protect", false, ""], [5, "Entities and Remaps", false, ""], [6, "Silence", false, ""], [7, "Client Print", false, ""], [8, "Shake Screen", false, ""], [9, "Kick / Kill Other", false, ""], [10, "Paralyze", false, ""], [11, "Give", false, ""], [12, "Scale", false, ""], [13, "Players", false, ""], [14, "Duel/Melee Arena", false, ""], [15, "Change Map", false, ""], [16, "Create Item", false, ""], [17, "God Mode / No Target", false, ""], [18, "Upgrade Levels", false, ""], [19, "Upgrade Skills", false, ""], [20, "Create Credits", false, ""], [21, "Ignore Chat Distance", false, ""], [22, "Give XP", false, ""], [23, "Update News", false, ""], [24, "Remove News", false, ""], [25, "Play Music", false, ""], [26, "Instant Revive", false, ""], [27, "Weather", false, ""], [28, "Player effects", false, ""]], "desc": "The admin powers a new account starts with: the same powers <code>/adminlist</code> lists and <code>/adminup</code> gives. <code>0</code> for none.", "presets": []}];

	var root = document.querySelector("[data-calculator]");
	if (!root) {
		return;
	}

	function el(tag, attrs, html) {
		var e = document.createElement(tag);
		Object.keys(attrs || {}).forEach(function (k) {
			e.setAttribute(k, attrs[k]);
		});
		if (html !== undefined) {
			e.innerHTML = html;
		}
		return e;
	}

	function parse(text) {
		text = String(text).trim();
		if (!/^-?\d+$/.test(text)) {
			return null;
		}
		var n = parseInt(text, 10);
		return (n >= -2147483648 && n <= 2147483647) ? n : null;
	}

	function copyText(text, button) {
		var done = function () {
			var old = button.textContent;
			button.textContent = "Copied";
			window.setTimeout(function () { button.textContent = old; }, 1400);
		};
		if (navigator.clipboard && navigator.clipboard.writeText) {
			navigator.clipboard.writeText(text).then(done, function () { fallback(); });
		} else {
			fallback();
		}
		function fallback() {
			var ta = el("textarea");
			ta.value = text;
			ta.setAttribute("readonly", "");
			ta.style.position = "fixed";
			ta.style.opacity = "0";
			document.body.appendChild(ta);
			ta.select();
			try {
				document.execCommand("copy");
				done();
			} catch (e) { /* the line is visible to copy by hand */ }
			document.body.removeChild(ta);
		}
	}

	SECTIONS.forEach(function (sec) {
		var allMask = 0;
		sec.bits.forEach(function (b) { allMask |= (1 << b[0]); });
		var value = 0;

		var card = el("section", { "class": "card calc-card", id: sec.cvar, "aria-labelledby": sec.cvar + "-name" });
		var head = el("div", { "class": "calc-head" });
		var title = el("div", { "class": "calc-title" });
		title.appendChild(el("h2", { id: sec.cvar + "-name" }, "<code>" + sec.cvar + "</code>"));
		title.appendChild(el("p", {}, sec.desc));
		head.appendChild(title);

		var controls = el("div", { "class": "calc-controls" });
		var inputId = "calc-" + sec.cvar;
		controls.appendChild(el("label", { "for": inputId, "class": "visually-hidden" }, "Value of " + sec.cvar));
		var input = el("input", { id: inputId, "class": "calc-value", type: "text", inputmode: "numeric", value: "0", autocomplete: "off", spellcheck: "false" });
		controls.appendChild(input);
		var allBtn = el("button", { type: "button", "class": "chip-btn" }, "All");
		var noneBtn = el("button", { type: "button", "class": "chip-btn" }, "None");
		controls.appendChild(allBtn);
		controls.appendChild(noneBtn);
		head.appendChild(controls);
		card.appendChild(head);

		if (sec.presets && sec.presets.length) {
			var presets = el("div", { "class": "calc-presets" }, "<span>Presets:</span>");
			sec.presets.forEach(function (p) {
				var b = el("button", { type: "button", "class": "chip-btn" }, p[0]);
				b.addEventListener("click", function () { set(p[1]); });
				presets.appendChild(b);
			});
			card.appendChild(presets);
		}

		var grid = el("div", { "class": "bit-grid" });
		var boxes = [];
		sec.bits.forEach(function (b) {
			if (b[2]) {
				return;	// hidden in the original too: kept in the value, not shown
			}
			var id = "bit-" + sec.cvar + "-" + b[0];
			var box = el("input", { type: "checkbox", id: id });
			box.setAttribute("data-bit", b[0]);
			var label = el("label", { "for": id, "class": "bit" });
			label.appendChild(box);
			label.appendChild(el("span", { "class": "bit-name" }, b[1] + (b[3] ? " <small>(" + b[3] + ")</small>" : "")));
			label.appendChild(el("span", { "class": "bit-num" }, String(Math.pow(2, b[0]))));
			box.addEventListener("change", function () {
				var bit = 1 << b[0];
				set(box.checked ? (value | bit) : (value & ~bit));
			});
			grid.appendChild(label);
			boxes.push(box);
		});
		card.appendChild(grid);

		var line = el("div", { "class": "calc-line" });
		var code = el("code", {});
		var copyBtn = el("button", { type: "button", "class": "chip-btn" }, "Copy");
		line.appendChild(code);
		line.appendChild(copyBtn);
		card.appendChild(line);
		copyBtn.addEventListener("click", function () { copyText(code.textContent, copyBtn); });

		function show() {
			boxes.forEach(function (box) {
				box.checked = !!(value & (1 << Number(box.getAttribute("data-bit"))));
			});
			code.textContent = "set " + sec.cvar + " \"" + value + "\"";
		}
		function set(v) {
			value = v | 0;
			input.value = String(value);
			input.removeAttribute("aria-invalid");
			show();
		}
		input.addEventListener("input", function () {
			var v = parse(input.value);
			if (v === null) {
				input.setAttribute("aria-invalid", "true");
				return;
			}
			input.removeAttribute("aria-invalid");
			value = v;
			show();
		});
		// as in the original: All is every bit the section knows, None is 0
		allBtn.addEventListener("click", function () { set(allMask); });
		noneBtn.addEventListener("click", function () { set(0); });
		show();
		root.appendChild(card);
	});
})();
