-- events: timed announcements, and /events to see what comes next.
-- Edit the table; "every" is in milliseconds. Enable with: rp_lua_plugins "events"

local announcements = {
	{ every = 600000,  text = "^3Tip: ^7/help lists the server's commands." },
	{ every = 900000,  text = "^3Tip: ^7/char lists your characters." },
	{ every = 1800000, text = "^3Event: ^7the arena opens at the top of the hour." },
}

local nextAt = {}	-- level.time of each entry's next announcement

local function announce(i)
	local ok, why = Msg.broadcast(announcements[i].text)
	if not ok then
		-- the server is busy: try once more in five seconds, then let it go
		Timer.after(5000, function() Msg.broadcast(announcements[i].text) end)
	end
	nextAt[i] = World.time() + announcements[i].every
end

Events.on("init", function()
	for i, a in ipairs(announcements) do
		nextAt[i] = World.time() + a.every
		Timer.every(a.every, function() announce(i) end)
	end
end)

Commands.add("events", function(p, args)
	local order = {}
	for i = 1, #announcements do order[i] = i end
	table.sort(order, function(a, b) return nextAt[a] < nextAt[b] end)
	local lines = {}
	for k = 1, math.min(3, #order) do
		local i = order[k]
		local minutes = math.max(0, math.floor((nextAt[i] - World.time()) / 60000 + 0.5))
		lines[k] = string.format("^7%d min: %s", minutes, announcements[i].text)
	end
	Msg.print(p, "^3Next: " .. table.concat(lines, " ^9| "))	-- one line: a message is one command, and newlines become spaces
end, "the next announcements")
