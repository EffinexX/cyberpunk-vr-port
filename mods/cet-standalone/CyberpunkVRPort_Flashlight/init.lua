-- CyberpunkVRPort_Flashlight -- binds the third-party Simple Flashlight mod to the headset and to a gesture.
--
-- THIS RUNS IN THE REAL CET (bin/x64/plugins/cyber_engine_tweaks/mods), NOT THE PORT'S LUA HOST. The flashlight mod
-- is installed in CET, GetMod only sees mods of its own Lua state, and the port's host is a separate one. Natives
-- (GetVRSharedSlot, VRViewWorldPos/Rot) are global RTTI functions and reach both.
--
-- Nothing of the flashlight mod is copied or edited: its table is reached through GetMod at run time.

local TOGGLE_SLOT = 175 -- vrshared::kFlashlightToggleCount

local flashlight = nil
local lastCount = nil

local function viewPose()
    if type(VRViewWorldPos) ~= "function" or type(VRViewWorldRot) ~= "function" then return nil end
    local pos = VRViewWorldPos()
    if pos == nil or pos.w == nil or pos.w <= 0.5 then return nil end
    return pos, VRViewWorldRot()
end

-- The quaternion's +Y axis, which is forward in the game's convention.
local function forwardOf(q)
    local x, y, z, w = q.i, q.j, q.k, q.r
    return 2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)
end

local function attach(mod)
    local logic = mod.logic
    local original = logic.updatePosition
    logic.updatePosition = function(fl)
        local pos, rot = viewPose()
        if pos == nil then return original(fl) end
        local light = Game.FindEntityByID(logic.lightID)
        if light == nil then return end
        local fx, fy, fz = forwardOf(rot)
        local reach = 0.2 + fl.settings.offset -- the mod's own distance, which keeps the body out of the beam
        local target = Vector4.new(pos.x + fx * reach, pos.y + fy * reach, pos.z + fz * reach, 1)
        Game.GetTeleportationFacility():Teleport(light, target, rot:ToEulerAngles())
    end
    flashlight = mod
    print("[CyberpunkVRPort_Flashlight] attached to the flashlight mod")
end

registerForEvent("onUpdate", function()
    if flashlight == nil then
        local mod = GetMod("flashlight")
        if mod == nil or mod.logic == nil or mod.logic.updatePosition == nil then return end
        attach(mod)
    end
    if type(GetVRSharedSlot) ~= "function" then return end

    local count = GetVRSharedSlot(TOGGLE_SLOT)
    if count == lastCount then return end
    local first = lastCount == nil
    lastCount = count
    if first then return end

    local run = flashlight.runtimeData
    if flashlight.archiveInstalled and run.inGame and not run.inMenu then
        flashlight.logic.toggle(flashlight)
    end
end)
