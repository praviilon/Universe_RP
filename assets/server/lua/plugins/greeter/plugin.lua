-- greeter: a welcome line with the character's visit count, and the map it was last seen on.
-- Enable with: rp_lua_plugins "greeter"  (or /scripts load greeter)

Events.on("character_select", function(e)
	local p = e.player
	local visits = (tonumber(Storage.char.get(p, "visits")) or 0) + 1
	Storage.char.set(p, "visits", visits)
	Storage.char.set(p, "last_map", World.mapname())
	if visits == 1 then
		Msg.print(p, "^7Welcome, " .. p.name .. "^7. This is your first visit.")
	else
		Msg.print(p, "^7Welcome back, " .. p.name .. "^7: visit " .. visits .. ".")
	end
end)

Events.on("client_disconnect", function(e)
	local p = e.player
	if p.char_id and p.char_id > 0 then
		Storage.char.set(p, "last_map", World.mapname())
	end
end)
