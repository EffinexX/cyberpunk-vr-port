-- WHAT A TOUCH SOUNDS LIKE: the gun or a hand meeting the world. The sweeps that stop them know where and how fast (VRMoverContact),
-- but not what of: a ray at that point, sent once a contact, asks the game for the material's name. All events are the game's own
-- (base\sound\event\eventsmetadata.json); which fits which material follows its physical_material_to_audio_metadata_matrix.
-- Played through the game's Wwise on emitters of our own, at the place of the touch and at a gain of our own (src/Audio/WwiseEmitter.cpp).
-- Pure Lua apart from what is handed to it in M.setup, so it can be run outside the game. See COLLISION_NOTES.md, "Contact sounds".
local M = {
    enabled = true,
    MIN_SPEED = 0.08,     -- m/s towards the surface: a slower touch is silent
    FULL_SPEED = 2.5,     -- m/s: full strength
    STRONG_FROM = 0.35,   -- strength from which a material uses its harder sound
    WEAPON_FROM = 0.25,   -- strength from which the weapon's own knock joins in
    COOLDOWN = 0.15,      -- s between two sounds of one body
    RELEASE = 0.12,       -- s without a contact before the next counts as a new one
    QUIETEST = 0.25,      -- of a slider's volume at the slowest touch; 1 at full strength
    SCRAPE_FROM = 0.4,    -- m/s along a surface from which a scrape starts
    SCRAPE_AFTER = 0.25,  -- s a contact must last before it can be a scrape: a knock is over by then
    SCRAPE_UNTIL = 0.15,  -- m/s along it under which it ends
    SCRAPE_FULL = 1.5,    -- m/s: full strength
    SCRAPE_LEVEL = 0.2,   -- of the objects slider, at full strength
    SCRAPE_PERSON = 0.6,  -- the same on a person: their slide is cloth, and much quieter than the others
    SCRAPE_LOST = 0.12,   -- s without a contact before it ends
    SCRAPE_RAY = 0.25,    -- s between asking again what it scrapes on
    LOG_MOST = 60,
}

-- In order: the first word found in the material's name decides. So 'plastic_car' is plastic, 'character_metal' metal and not flesh.
local GROUP_OF = {
    { 'glass', 'glass' }, { 'plexi', 'glass' }, { 'neon', 'glass' },
    { 'ceramic', 'ceramic' },
    { 'plastic', 'plastic' }, { 'rubber', 'plastic' }, { 'tire', 'plastic' },
    { 'metal_car', 'car' }, { 'vehicle', 'car' },
    { 'metal_hollow', 'metal_hollow' }, { 'metal_pipe', 'metal_hollow' },
    { 'metal', 'metal' }, { 'character_armor', 'metal' },
    { 'character', 'flesh' }, { 'cyberware', 'flesh' }, { 'flesh', 'flesh' }, { 'meat', 'flesh' },
    { 'wood', 'wood' },
    { 'cardboard', 'cardboard' }, { 'paper', 'paper' },
    { 'carpet', 'cloth' }, { 'cloth', 'cloth' }, { 'upholstery', 'cloth' }, { 'fabric', 'cloth' }, { 'furniture', 'cloth' }, { 'leather', 'cloth' },
    { 'trash', 'trash' }, { 'food', 'food' },
    { 'dirt', 'dirt' }, { 'mud', 'dirt' }, { 'grass', 'dirt' }, { 'sand', 'dirt' },
    { 'tile', 'tile' }, { 'linoleum', 'tile' },
    { 'concrete', 'concrete' },   -- before water: concrete_water_puddles
    { 'water', 'water' },
}

-- weak: a light touch. strong: a harder one. hand: what a hand makes on it. Empty: silent.
local SOUNDS = {
    concrete     = { weak = 'ph_concrete_hard', strong = 'ph_concrete_hard', hand = 'lcm_fs_npc_additional_concrete_hand_grab' },
    tile         = { weak = 'ph_concrete_hard', strong = 'ph_concrete_hard', hand = 'lcm_fs_npc_additional_tiles_hand_grab' },
    metal        = { weak = 'ph_metal_tray_solid', strong = 'ph_metal_hard', hand = 'lcm_fs_npc_additional_metal_tin_hand_grab' },
    metal_hollow = { weak = 'ph_metal_tray_solid', strong = 'ph_metal_hollow_hard', hand = 'lcm_fs_npc_additional_metal_hollow_hand_grab' },
    car          = { weak = 'ph_metal_bucket_solid', strong = 'ph_metal_hard', hand = 'lcm_fs_npc_additional_metal_hollow_hand_grab' },
    wood         = { weak = 'ph_wood_solid', strong = 'ph_wood_solid', hand = 'lcm_fs_npc_additional_wood_hollow_hand_grab' },
    plastic      = { weak = 'ph_plastic_light_solid', strong = 'ph_plastic_solid', hand = 'lcm_fs_npc_additional_plastic_hand_grab' },
    glass        = { weak = 'ph_glass_cup_solid', strong = 'ph_glass_hard', hand = 'lcm_fs_npc_additional_glass_hand_grab' },
    ceramic      = { weak = 'ph_ceramic_solid', strong = 'ph_ceramic_solid', hand = 'lcm_fs_npc_additional_glass_hand_grab' },
    cardboard    = { weak = 'ph_cardboard_soft', strong = 'ph_cardboard_soft', hand = 'lcm_fs_npc_additional_paper_hand_grab' },
    paper        = { weak = 'ph_paper_soft', strong = 'ph_paper_soft', hand = 'lcm_fs_npc_additional_paper_hand_grab' },
    cloth        = { weak = 'ph_carpet_soft', strong = 'ph_carpet_soft', hand = 'lcm_fs_npc_additional_carpet_hand_grab' },
    trash        = { weak = 'ph_trash_soft', strong = 'ph_trash_soft', hand = 'lcm_fs_npc_additional_tarp_hand_grab' },
    food         = { weak = 'ph_food_soft', strong = 'ph_food_soft', hand = 'lcm_fs_npc_additional_tarp_hand_grab' },
    dirt         = { weak = 'lcm_fs_npc_additional_dirt_hand_grab', strong = 'lcm_fs_npc_additional_dirt_hand_grab', hand = 'lcm_fs_npc_additional_dirt_hand_grab' },
    flesh        = { weak = 'ph_leather_soft', strong = 'ph_leather_soft', hand = 'ph_leather_soft' },
    water        = {},
}
M.GROUP_OF, M.SOUNDS = GROUP_OF, SOUNDS

-- What a scrape along it sounds like: the game's slides of a body along a surface, the NPCs' ones: the player's have no attenuation and do not come from a place. No NPC slide on plastic. None for what should not scrape.
M.SLIDE = {
    concrete = 'lcm_fs_npc_additional_concrete_slide', tile = 'lcm_fs_npc_additional_tiles_slide', metal = 'lcm_fs_npc_additional_metal_tin_slide',
    metal_hollow = 'lcm_fs_npc_additional_metal_hollow_slide', car = 'lcm_fs_npc_additional_metal_hollow_slide', wood = 'lcm_fs_npc_additional_wood_hollow_slide',
    plastic = 'lcm_fs_npc_additional_linoleum_slide', glass = 'lcm_fs_npc_additional_glass_slide', ceramic = 'lcm_fs_npc_additional_tiles_slide',
    cardboard = 'lcm_fs_npc_additional_paper_slide', paper = 'lcm_fs_npc_additional_paper_slide', cloth = 'lcm_fs_npc_additional_carpet_slide',
    trash = 'lcm_fs_npc_additional_tarp_slide', food = 'lcm_fs_npc_additional_tarp_slide', dirt = 'lcm_fs_npc_additional_dirt_slide',
    flesh = 'lcm_fs_npc_additional_carpet_slide',   -- cloth, on a person
}
-- The gun knocks are one event behind this switch; a surface's state. See COLLISION_NOTES.md.
M.KNOCK_GROUP = 'w_melee_material_light'
M.KNOCK_EVENT = { w_gun_pistol_melee_hit = true, w_gun_rifle_melee_hit = true, w_gun_lmg_melee_hit = true }
M.KNOCK_STATE = {
    concrete = 'concrete', tile = 'concrete', dirt = 'concrete', metal = 'metal', metal_hollow = 'metal', car = 'metal', wood = 'wood', plastic = 'wood',
    glass = 'glass', ceramic = 'glass', cardboard = 'cloth', paper = 'cloth', cloth = 'cloth', trash = 'cloth', food = 'cloth', flesh = 'skin', water = 'cloth',
}
M.TOUCH_RELEASE = 0.3   -- s out of a loose thing before touching it again counts

-- The weapon's own knock, by the record's item type. Melee events take the game's volume parameter.
local WEAPON_KNOCK = {
    Wea_Handgun = 'w_gun_pistol_melee_hit', Wea_Revolver = 'w_gun_pistol_melee_hit',
    Wea_AssaultRifle = 'w_gun_rifle_melee_hit', Wea_Rifle = 'w_gun_rifle_melee_hit', Wea_SubmachineGun = 'w_gun_rifle_melee_hit',
    Wea_Shotgun = 'w_gun_rifle_melee_hit', Wea_ShotgunDual = 'w_gun_rifle_melee_hit', Wea_SniperRifle = 'w_gun_rifle_melee_hit', Wea_PrecisionRifle = 'w_gun_rifle_melee_hit',
    Wea_LightMachineGun = 'w_gun_lmg_melee_hit', Wea_HeavyMachineGun = 'w_gun_lmg_melee_hit',
    Wea_Katana = 'w_melee_katana_impact_basic', Wea_Sword = 'w_melee_katana_impact_basic', Wea_Knife = 'w_melee_katana_impact_basic', Wea_ShortBlade = 'w_melee_katana_impact_basic', Wea_LongBlade = 'w_melee_katana_impact_basic', Wea_Axe = 'w_melee_katana_impact_basic', Wea_Chainsword = 'w_melee_katana_impact_basic', Wea_Machete = 'w_melee_katana_impact_basic',
    Wea_Hammer = 'w_melee_blunt_impact_metal', Wea_TwoHandedClub = 'w_melee_blunt_impact_metal', Wea_OneHandedClub = 'w_melee_blunt_impact_base',
}
M.WEAPON_KNOCK = WEAPON_KNOCK

local env = {}      -- staticRay(a, b), dynamicRay(a, b), vec(x, y, z), nameOf(material), contact(id, what), now(), play(event, entity), wwise(event, x, y, z, gain, group, state), scrape(body, event, x, y, z, gain), anyRay(a, b), personNear(x, y, z), slider(which), say(text), knockOf(weapon)
local state = {}
M.stats = { notWorld = 0, scrapes = 0, contacts = 0, exact = 0, found = 0, staticHit = 0, dynamicHit = 0, missed = 0, played = 0, playFailed = 0 }

function M.setup(e) env = e end

function M.groupOf(name)
    if not name then return 'concrete' end
    local low = name:lower()
    for i = 1, #GROUP_OF do
        if low:find(GROUP_OF[i][1], 1, true) then return GROUP_OF[i][2] end
    end
    return 'concrete'
end

function M.strengthOf(speed)
    local s = (speed - M.MIN_SPEED) / (M.FULL_SPEED - M.MIN_SPEED)
    return s < 0.0 and 0.0 or s > 1.0 and 1.0 or s
end

-- The name of what is at the point. Straight in along the normal; then from further out and further in; then a little to the side, for an edge.
function M.materialAt(px, py, pz, nx, ny, nz, exact, moving, aim)
    -- a loose thing: straight at its own middle, from the part that is in it
    if aim then
        local dx, dy, dz = aim.x - px, aim.y - py, aim.z - pz
        local len = math.sqrt(dx * dx + dy * dy + dz * dz)
        if len > 0.001 then
            local a = env.vec(px, py, pz)
            local b = env.vec(aim.x + dx / len * 0.1, aim.y + dy / len * 0.1, aim.z + dz / len * 0.1)
            local tr = env.dynamicRay(a, b)
            local how = 'dynamic'
            if not tr then tr, how = env.anyRay(a, b), 'any' end
            if tr then
                local name = env.nameOf(tr.material)
                if name and name ~= '' and name ~= 'None' then return name, how, 'aim' end
            end
        end
    end
    local deep = exact and 0.06 or 0.35
    -- two directions across the normal
    local sx, sy, sz = ny, -nx, 0.0
    if sx * sx + sy * sy < 1e-4 then sx, sy, sz = 0.0, nz, -ny end
    local len = math.sqrt(sx * sx + sy * sy + sz * sz)
    sx, sy, sz = sx / len * 0.015, sy / len * 0.015, sz / len * 0.015
    local tries = {
        { 0.015, deep, 0.0, 0.0, 0.0 },
        { 0.04, deep + 0.15, 0.0, 0.0, 0.0 },
        { 0.04, deep + 0.15, sx, sy, sz },
    }
    for i = 1, #tries do
        local t = tries[i]
        local a = env.vec(px + nx * t[1] + t[3], py + ny * t[1] + t[4], pz + nz * t[1] + t[5])
        local b = env.vec(px - nx * t[2] + t[3], py - ny * t[2] + t[4], pz - nz * t[2] + t[5])
        -- what moves first when it was something moving, or a wall behind it would answer
        local first, firstHow, second, secondHow = env.staticRay, 'static', env.dynamicRay, 'dynamic'
        if moving then first, firstHow, second, secondHow = env.dynamicRay, 'dynamic', env.staticRay, 'static' end
        local tr, how = first(a, b), firstHow
        if not tr then tr, how = second(a, b), secondHow end
        if not tr and moving then tr, how = env.anyRay(a, b), 'any' end
        if tr then
            local name = env.nameOf(tr.material)
            if name and name ~= '' and name ~= 'None' then return name, how, i end
        end
    end
    return nil
end

-- One event, at a gain: through our emitter, or if Wwise will not, on the entity at the game's own loudness.
local function playOne(event, entity, gain, x, y, z, group, state)
    if gain <= 0.0 then return end
    if env.wwise(event, x, y, z, gain, group or '', state or '') then
        M.stats.played = M.stats.played + 1
    elseif env.play(event, entity) then
        M.stats.played = M.stats.played + 1
    else
        M.stats.playFailed = M.stats.playFailed + 1
    end
end

-- What is played for one contact. Returns the names, for the log.
function M.sound(isHand, name, strength, entity, knock, x, y, z)
    local group = M.groupOf(name)
    local set = SOUNDS[group] or SOUNDS.concrete
    local event
    if isHand then event = set.hand
    else event = strength >= M.STRONG_FROM and set.strong or set.weak end
    local loud = M.QUIETEST + (1.0 - M.QUIETEST) * strength
    if event then playOne(event, entity, loud * env.slider(0), x, y, z) end
    local joined
    if not isHand and knock and strength >= M.WEAPON_FROM and event then
        local switchGroup, switchState
        if M.KNOCK_EVENT[knock] then switchGroup, switchState = M.KNOCK_GROUP, M.KNOCK_GROUP .. '_' .. (M.KNOCK_STATE[group] or 'concrete') end
        playOne(knock, entity, loud * env.slider(1), x, y, z, switchGroup, switchState)
        joined = knock .. (switchState and (' ' .. switchState) or '')
    end
    return event, joined
end

local function fire(id, kind, px, py, pz, nx, ny, nz, exact, moving, speed, entity, weapon, aim, about, known)
    local st = M.stats
    st.contacts = st.contacts + 1
    if exact then st.exact = st.exact + 1 end
    local name, how, try = known, 'solver', nil
    if not known then name, how, try = M.materialAt(px, py, pz, nx, ny, nz, exact, moving, aim) end
    -- a person: their colliders answer few rays, and what does answer may have no material of its own
    if kind == 'moving' and (not name or M.groupOf(name) == 'concrete') and env.personNear(px, py, pz) then
        name, how = 'character_flesh.physmat', 'person near'
    end
    if name then
        st.found = st.found + 1
        if how == 'static' then st.staticHit = st.staticHit + 1 else st.dynamicHit = st.dynamicHit + 1 end
    else
        st.missed = st.missed + 1
        -- no answer on something that moves: what such things mostly are, not the concrete of the world
        if moving then name, how = kind == 'loose' and 'plastic.physmat' or 'metal.physmat', 'guess' end
    end
    local knock = id == 2 and weapon and env.knockOf(weapon) or nil
    local event, joined = M.sound(id ~= 2, name, M.strengthOf(speed), entity, knock, px, py, pz)
    if kind ~= 'world' then st.notWorld = st.notWorld + 1 end
    if st.contacts <= M.LOG_MOST or st.contacts % 25 == 0 or (kind ~= 'world' and st.notWorld <= M.LOG_MOST) then
        env.say(string.format('[contact] %s, %s: %s (%s, ray %s, point %s) -> %s%s, %.2f m/s; so far %d contacts, %d exact points, %d found (%d static, %d dynamic), %d missed, %d failed to play, %d scrapes',
            id == 2 and 'gun' or id == 0 and 'left hand' or 'right hand', kind .. (about and (' ' .. about) or ''), tostring(name), tostring(how), tostring(try), exact and 'exact' or 'part middle',
            tostring(event), joined and (' + ' .. joined) or '', speed, st.contacts, st.exact, st.found, st.staticHit, st.dynamicHit, st.missed, st.playFailed, st.scrapes))
    end
end

local function stateOf(id)
    local s = state[id]
    if not s then s = { seen = -1000.0, played = -1000.0 }; state[id] = s end
    return s
end

local function endScrape(id, s)
    if s.scraping then
        s.scraping = false
        s.scrapeName = nil
        env.scrape(id, '', 0.0, 0.0, 0.0, 0.0)
    end
end

-- A hand that stops being asked about (it took something, or took the gun).
function M.scrapeEnd(id)
    if state[id] then endScrape(id, state[id]) end
end

-- Along a surface. The sweeps report a contact for each frame it presses on it.
local function scrape(id, s, n)
    local now = env.now()
    if n and n.w == 0.0 then
        local v = env.contact(id, 2)
        if not s.touchAt or now - s.touchAt > M.SCRAPE_LOST then s.touchSince, s.slide = now, 0.0 end
        s.slide = s.slide * 0.7 + (v and v.z or 0.0) * 0.3
        s.touchAt = now
    end
    if not s.touchAt or now - s.touchAt > M.SCRAPE_LOST or (s.slide or 0.0) < (s.scraping and M.SCRAPE_UNTIL or M.SCRAPE_FROM) then
        endScrape(id, s)
        return
    end
    if not n or n.w ~= 0.0 or (not s.scraping and now - s.touchSince < M.SCRAPE_AFTER) then return end
    local p = env.contact(id, 0)
    if not p then return end
    if not s.scrapeName or now - (s.scrapeAsked or -1000.0) > M.SCRAPE_RAY then
        s.scrapeAsked = now
        local moved = env.contact(id, 2)
        local name = M.materialAt(p.x, p.y, p.z, n.x, n.y, n.z, p.w > 0.5, moved and moved.y > 0.5)
        s.scrapeName = name or s.scrapeName or 'concrete.physmat'
    end
    local event = M.SLIDE[M.groupOf(s.scrapeName)]
    local top = (s.slide - M.SCRAPE_UNTIL) / (M.SCRAPE_FULL - M.SCRAPE_UNTIL)
    top = top > 1.0 and 1.0 or top
    local gain = env.slider(0) * M.SCRAPE_LEVEL * (M.QUIETEST + (1.0 - M.QUIETEST) * top)
    if not event or gain <= 0.0 then
        endScrape(id, s)
        return
    end
    if env.scrape(id, event, p.x, p.y, p.z, gain) then
        if not s.scraping then M.stats.scrapes = M.stats.scrapes + 1 end
        s.scraping = true
    end
end

-- Each frame, for one body: 0 the left hand, 1 the right, 2 the gun. What a sweep stopped it at.
function M.tick(id, entity, weapon)
    if not M.enabled then return end
    local n = env.contact(id, 1)
    local s = stateOf(id)
    scrape(id, s, n)
    if not n or n.w ~= 0.0 then return end
    local now = env.now()
    local gap = now - s.seen
    s.seen = now
    if gap < M.RELEASE or now - s.played < M.COOLDOWN then return end
    local speedV = env.contact(id, 2)
    local speed = speedV and speedV.x or 0.0
    if speed < M.MIN_SPEED then return end
    local p = env.contact(id, 0)
    if not p then return end
    s.played = now
    local moving = speedV.y > 0.5
    fire(id, moving and 'moving' or 'world', p.x, p.y, p.z, n.x, n.y, n.z, p.w > 0.5, moving, speed, entity, weapon)
end

local touched = {}
local loose, looseCount = {}, 0
-- A loose thing the body is in (they do not stop it, so no sweep tells of them). key: the thing. at: the middle of the part in it. away: the way out of it.
-- A person: the capsule solve has told where, so no ray is asked. They are never loose, or anything else.
-- Along a person: the capsule solve knows it presses on one, and the mover how fast the hand goes along them.
function M.personSlide(id, x, y, z, slide)
    if not M.enabled then return end
    local s = stateOf(id)
    local now = env.now()
    if not s.touchAt or now - s.touchAt > M.SCRAPE_LOST then s.touchSince, s.slide = now, 0.0 end
    s.slide = s.slide * 0.7 + slide * 0.3
    s.touchAt = now
    if s.slide < (s.scraping and M.SCRAPE_UNTIL or M.SCRAPE_FROM) then
        endScrape(id, s)
        return
    end
    if not s.scraping and now - s.touchSince < M.SCRAPE_AFTER then return end
    local top = (s.slide - M.SCRAPE_UNTIL) / (M.SCRAPE_FULL - M.SCRAPE_UNTIL)
    top = top > 1.0 and 1.0 or top
    local gain = env.slider(0) * M.SCRAPE_PERSON * (M.QUIETEST + (1.0 - M.QUIETEST) * top)
    if gain <= 0.0 then
        endScrape(id, s)
        return
    end
    if env.scrape(id, M.SLIDE.flesh, x, y, z, gain) then
        if not s.scraping then M.stats.scrapes = M.stats.scrapes + 1 end
        s.scraping = true
    end
end

function M.person(id, who, at, away, speed, entity, weapon)
    if not M.enabled then return end
    local now = env.now()
    local s = stateOf(id)
    local key = 'person' .. tostring(who)
    if now - (touched[key] or -1000.0) < M.TOUCH_RELEASE or now - s.played < M.COOLDOWN or speed < M.MIN_SPEED then
        touched[key] = now
        return
    end
    touched[key] = now
    s.played = now
    fire(id, 'person', at.x, at.y, at.z, away.x, away.y, away.z, true, true, speed, entity, weapon, nil, nil, 'character_flesh.physmat')
end

function M.touch(id, key, at, away, speed, entity, weapon, aim, about)
    if not M.enabled then return end
    local now = env.now()
    local e = loose[key]
    if not e then
        if looseCount >= 256 then loose, looseCount = {}, 0 end
        looseCount = looseCount + 1
        e = { seen = -1000.0, fired = false }
        loose[key] = e
    end
    if now - e.seen >= M.TOUCH_RELEASE then e.fired = false end
    e.seen = now
    if e.fired then return end
    local s = stateOf(id)
    if now - s.played < M.COOLDOWN then return end
    -- not yet: a touch may start slower than it goes on, and this frame is not the last of it
    if speed < M.MIN_SPEED then return end
    e.fired = true
    s.played = now
    fire(id, 'loose', at.x, at.y, at.z, away.x, away.y, away.z, false, true, speed, entity, weapon, aim, about)
end

return M
