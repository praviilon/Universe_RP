-- quest: a guard, a crate, a swoop, a button and the quest-giver's dialogue with its own reward. Shows every
-- P2 call: World.npc, World.prop (props and the quest entities), World.vehicle, World.watch + entity_used,
-- Effect.play, Sound.play, World.use, World.remove, the npc_death and dialog_answer events, Storage.char.
-- Edit the table for your map. Enable with: rp_lua_plugins "quest"  (or /scripts load quest)

local setup = {
	map        = "mp/ffa5",                                 -- only on this map
	npc        = { type = "jedi", origin = { x = 0, y = 0, z = 64 }, angles = { x = 0, y = 90, z = 0 },
	               credits = 200, xp = 10,                  -- paid to the player who defeats the guard
	               team = "enemy", health = 300 },          -- /npc kill lua.quest.guard removes it
	crate      = { origin = { x = 128, y = 0, z = 32 }, model = "models/map_objects/imp_mine/crate.md3" },
	vehicle    = { type = "swoop", origin = { x = -128, y = 0, z = 32 }, angles = { x = 0, y = 90, z = 0 } },
	button     = "quest_button",                            -- a map or entity-system button's targetname
	reward     = "quest_door",                              -- what the button's reward uses (a door, a target_reward...)
	pay        = { credits = 100, xp = 5 },                 -- the dialogue's own reward, paid once per character
	effect     = "env/small_fire",                          -- must already be registered on the map
	sound      = "sound/movers/switches/switch3.wav",       -- same; played when the guard falls
	respawn_ms = 60000,
}

local guard, crate, bike, talk

local function place()
	if World.mapname() ~= setup.map then return end
	local why
	guard, why = World.npc(setup.npc.type, setup.npc.origin, setup.npc.angles,
		{ name = "guard", credits = setup.npc.credits, xp = setup.npc.xp, team = setup.npc.team, health = setup.npc.health })
	if not guard then Log.info("no guard: " .. tostring(why)) end
	crate, why = World.prop("misc_model_breakable", setup.crate.origin, nil, { model = setup.crate.model, health = 100 }, { name = "crate" })
	if not crate then Log.info("no crate: " .. tostring(why)) end
	-- players may ride it; if someone is on it when the plugin stops, it stays until they get off
	bike, why = World.vehicle(setup.vehicle.type, setup.vehicle.origin, setup.vehicle.angles, { name = "bike" })
	if not bike then Log.info("no swoop: " .. tostring(why)) end
	-- the quest-giver's dialogue, with a reward and a flag of the plugin's own: every link (go<n>, target) names one of
	-- this plugin's entities, never the map's. "Done" is offered only after the button set the flag; it pays once each.
	World.prop("target_reward", setup.npc.origin, nil, { credits = setup.pay.credits, xp = setup.pay.xp, message = "Quest reward", spawnflags = 1 }, { name = "pay" })
	World.prop("target_flag", setup.npc.origin, nil, { flag = "quest_button_pressed" }, { name = "flag" })
	talk, why = World.prop("target_dialog", setup.npc.origin, nil, {
		title = "Guard", message = "Press the button by the crate, then come back.",
		option1 = "Done", need1 = "quest_button_pressed", go1 = "pay",
		option2 = "On my way", timeout = 30, speaker = "guard" }, { name = "talk" })
	if not talk then Log.info("no dialogue: " .. tostring(why)) end
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
	World.use("lua.quest.flag", p)  -- the dialogue's "Done" is offered from now on
	-- the reward is whatever the map does when that name is used, with this player as activator:
	-- a door opens for them, a teleporter sends them, a target_give gives them the map's items,
	-- a target_reward (placed with /entadd) pays them
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
			local g, why = World.npc(setup.npc.type, setup.npc.origin, setup.npc.angles,
				{ name = "guard", credits = setup.npc.credits, xp = setup.npc.xp, team = setup.npc.team, health = setup.npc.health })
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

Commands.add("guard", function(p)
	-- opens the guard's dialogue for the player (a trigger on the guard would do the same); their answer is their own
	local used, why = World.use("lua.quest.talk", p)
	if used == false then Msg.print(p, "^3Quest: ^7" .. tostring(why)) end
end, "talk to the guard")

Events.on("dialog_answer", function(e)
	if e.dialog == "lua.quest.talk" and e.reason == "answered" then Log.info(e.player.clean_name .. " answered: " .. e.option) end
end)
