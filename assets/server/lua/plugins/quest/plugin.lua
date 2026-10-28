-- quest: a guard, a crate and a button. Shows every P2 call: World.npc, World.prop, World.watch +
-- entity_used, Effect.play, Sound.play, World.use, World.remove, the npc_death event, Storage.char.
-- Edit the table for your map. Enable with: rp_lua_plugins "quest"  (or /scripts load quest)

local setup = {
	map        = "mp/ffa5",                                 -- only on this map
	npc        = { type = "jedi", origin = { x = 0, y = 0, z = 64 }, angles = { x = 0, y = 90, z = 0 } },
	crate      = { origin = { x = 128, y = 0, z = 32 }, model = "models/map_objects/imp_mine/crate.md3" },
	button     = "quest_button",                            -- a map or entity-system button's targetname
	reward     = "quest_door",                              -- what the button's reward uses (a door, a target_teleporter...)
	effect     = "env/small_fire",                          -- must already be registered on the map
	sound      = "sound/movers/switches/switch3.wav",       -- same; played when the guard falls
	respawn_ms = 60000,
}

local guard, crate

local function place()
	if World.mapname() ~= setup.map then return end
	local why
	guard, why = World.npc(setup.npc.type, setup.npc.origin, setup.npc.angles, { name = "guard" })
	if not guard then Log.info("no guard: " .. tostring(why)) end
	crate, why = World.prop("misc_model_breakable", setup.crate.origin, nil, { model = setup.crate.model, health = 100 }, { name = "crate" })
	if not crate then Log.info("no crate: " .. tostring(why)) end
end

Events.on("init", function()
	place()
	World.watch(setup.button)
end)

Events.on("entity_used", function(e)
	if e.targetname ~= setup.button or not e.activator then return end
	local p = e.activator
	if not p.char_id or p.char_id <= 0 then
		Msg.print(p, "^3Quest: ^7log in and pick a character first.")
		return
	end
	local step = tonumber(Storage.char.get(p, "quest_step")) or 0
	if step >= 1 then
		Msg.print(p, "^3Quest: ^7you have done this already.")
		return
	end
	Storage.char.set(p, "quest_step", 1)
	-- three actions, three tokens of the bucket of four (one comes back a second): keep a handler short
	if crate and crate:valid() then
		Effect.play(setup.effect, crate.origin, nil)
	end
	-- the reward is whatever the map does when that name is used, with this player as activator:
	-- a door opens for them, a teleporter sends them, a target_give gives them the map's items
	local used, why = World.use(setup.reward, p)
	if used == false then Log.info("reward: " .. tostring(why)) end
	Msg.print(p, "^3Quest: ^7well done, " .. p.name .. "^7.")
end)

Events.on("npc_death", function(e)
	if guard and e.entity == guard then
		Sound.play(setup.sound, e.entity.origin)
		Msg.broadcast("^3Quest: ^7the guard was defeated" .. (e.killer and (" by " .. e.killer.name) or "") .. "^7. Another comes in a minute.")
		Timer.after(setup.respawn_ms, function()
			if guard and guard:valid() then World.remove(guard) end  -- the body, if it is still there
			local g, why = World.npc(setup.npc.type, setup.npc.origin, setup.npc.angles, { name = "guard" })
			guard = g
			if not g then Log.info("no guard: " .. tostring(why)) end
		end)
	end
end)

Commands.add("quest", function(p)
	if not p.char_id or p.char_id <= 0 then Msg.print(p, "^3Quest: ^7log in and pick a character first.") return end
	local step = tonumber(Storage.char.get(p, "quest_step")) or 0
	Msg.print(p, step >= 1 and "^3Quest: ^7done." or "^3Quest: ^7find the button near the guard.")
end, "your quest progress")
