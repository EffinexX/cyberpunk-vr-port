-- Read the player's gamepad range and remove model input slew only during a VR
-- wheel grab. The model's steering limits, speed assists and tire physics stay
-- native. Shared drive-model flats are restored when the grip/driver changes.
local patch = nil
local profileTime = 0.5
local reportedError = false
local rates = {
    turnUpdateInputSlowChangeSpeed = 100.0,
    turnUpdateInputFastChangeSpeed = 100.0,
    wheelTurnMaxAddPerSecond = 10000.0,
    wheelTurnMaxSubPerSecond = 10000.0,
}

local function flat(id, name) return TweakDBID.new(id, "." .. name) end
local function key(id) return tostring(id.hash) .. ":" .. tostring(id.length) end
local function same(a, b) return type(a) == "number" and math.abs(a - b) < 0.00001 end

local function restore()
    if not patch then return end
    for name, entry in pairs(patch.values) do
        local id = flat(patch.id, name)
        -- An external edit made while held belongs to its author, not this mod.
        if same(TweakDB:GetFlat(id), entry.applied) then
            assert(TweakDB:SetFlat(id, entry.before) ~= false, "cannot restore steering rate")
        end
        patch.values[name] = nil
    end
    patch = nil
end

local function apply(id)
    local wanted = key(id)
    if patch and patch.key == wanted then return end
    restore()
    local values = {}
    for name, rate in pairs(rates) do
        local before = TweakDB:GetFlat(flat(id, name))
        -- Not all mounted vehicle types use the road-vehicle drive model.
        if type(before) ~= "number" then return end
        values[name] = { before = before, applied = math.max(before, rate) }
    end
    patch = { id = id, key = wanted, values = values }
    for name, entry in pairs(values) do
        local field = flat(id, name)
        assert(TweakDB:SetFlat(field, entry.applied) ~= false, "cannot apply steering rate")
        assert(same(TweakDB:GetFlat(field), entry.applied), "steering rate not accepted")
    end
end

-- LOCAL DIAGNOSTICS: why a hand does not take (or loses) the wheel. Logs only on changes.
-- state: 0 = wheel unavailable (not driving / game menu mode / overlay), 4 = available,
-- +1 right hand held, +2 left hand held. armed: wheel proximity bits (1 right, 2 left).
local function diagPrint(msg)
    print(msg)
    pcall(function()
        local f = io.open("diag.log", "a")
        if f then f:write(os.date("[%Y-%m-%d %H:%M:%S] ") .. msg .. "
"); f:close() end
    end)
end
local diag = { mounted = false, state = nil, held = 0, grip = { false, false }, pend = {} }
local function bit(v, b) return math.floor(v / b) % 2 == 1 end
local function wheelDiag(dt)
    local player = Game.GetPlayer()
    local vehicle = player and player:GetMountedVehicle()
    if not vehicle then
        if diag.mounted then diagPrint("[driving] diag: left the vehicle") end
        diag.mounted, diag.state, diag.held, diag.pend = false, nil, 0, {}
        return
    end
    local state = Game.GetVRWheelControlState() or 0
    local slot = type(GetVRSharedSlot) == "function" and GetVRSharedSlot or function() return 0 end
    local grip = { (slot(49) or 0) > 0.5, (slot(155) or 0) > 0.5 }
    local armed = math.floor((slot(163) or 0) + 0.5)
    local held = state % 4
    local function context()
        local weapon, driver = "none", "?"
        pcall(function()
            local w = Game.GetTransactionSystem():GetItemInSlot(player, TweakDBID.new("AttachmentSlots.WeaponRight"))
            if w then weapon = TDBID.ToStringDEBUG(w:GetItemID().id) end
        end)
        pcall(function() driver = tostring(VehicleComponent.IsDriver(player)) end)
        return string.format("state=%d armed=%d grips R=%s L=%s weapon=%s driver=%s",
            state, armed, tostring(grip[1]), tostring(grip[2]), weapon, driver)
    end
    if not diag.mounted then
        diag.mounted = true
        diagPrint("[driving] diag: entered vehicle, " .. context())
    elseif state - held ~= (diag.state or 0) - diag.held then
        diagPrint(string.format("[driving] diag: wheel %s, %s", state >= 4 and "available" or "UNAVAILABLE", context()))
    end
    for h = 1, 2 do
        local b = h == 1 and 1 or 2
        local name = h == 1 and "right" or "left"
        local was, now = bit(diag.held, b), bit(held, b)
        if was ~= now then
            diagPrint(string.format("[driving] diag: %s hand %s the wheel%s, %s", name, now and "took" or "released",
                (not now and grip[h]) and " WHILE GRIP STILL HELD" or "", context()))
        end
        if grip[h] and not diag.grip[h] and not now then diag.pend[h] = 0 end
        if diag.pend[h] then
            if not grip[h] or now then
                diag.pend[h] = nil
            else
                diag.pend[h] = diag.pend[h] + math.max(0, dt or 0)
                if diag.pend[h] >= 0.3 then
                    diag.pend[h] = nil
                    diagPrint(string.format("[driving] diag: %s grip pressed but no grab (%s), %s", name,
                        bit(armed, b) and "hand at the wheel" or "hand NOT near the wheel", context()))
                end
            end
        end
    end
    diag.state, diag.held, diag.grip = state, held, grip
end

registerForEvent("onUpdate", function(dt)
    local dok, derr = pcall(wheelDiag, dt)
    if not dok and not diag.err then diag.err = true; diagPrint("[driving] diag error: " .. tostring(derr)) end
    local ok, err = pcall(function()
        local state = Game.GetVRWheelControlState()
        if state == 0 then restore(); profileTime = 0.5; return end
        profileTime = profileTime + math.max(0, dt or 0)
        if profileTime >= 0.5 then
            local settings = Game.GetSettingsSystem()
            local inner = settings:GetVar("/controls", "Axis_DeadzoneInnerFix"):GetValue()
            local outer = settings:GetVar("/controls", "Axis_DeadzoneOuter"):GetValue()
            assert(Game.SetVRWheelGamepadProfile(inner, outer), "invalid gamepad range")
            profileTime = 0
        end
        if state % 4 == 0 then restore(); return end
        local player = Game.GetPlayer()
        local vehicle = player and player:GetMountedVehicle()
        if not vehicle then restore(); return end
        local model = TweakDB:GetFlat(flat(vehicle:GetRecordID(), "vehDriveModelData"))
        if not model then restore(); return end
        apply(model)
    end)
    if not ok then
        pcall(restore)
        if not reportedError then print("[driving] wheel profile unavailable: " .. tostring(err)) end
        reportedError = true
    else
        reportedError = false
    end
end)

registerForEvent("onShutdown", function() pcall(restore) end)
return { GetState = function() return { active = patch ~= nil, model = patch and patch.key or nil } end }
