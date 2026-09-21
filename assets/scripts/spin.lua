-- Sample gameplay script: spins the entity it is attached to.
-- Attach it from the Inspector (Script > Script Asset) to any entity.
local speed = 45.0

function OnStart(self)
    -- Properties set in the Inspector are available as self.Properties
    if self.Properties and self.Properties.speed then
        speed = tonumber(self.Properties.speed) or speed
    end
    Log("spin.lua started on " .. self.Name)
end

function OnUpdate(self, dt)
    local yaw = self.Transform.Rotation.y + speed * dt
    self.Transform.Rotation = { x = self.Transform.Rotation.x, y = yaw, z = self.Transform.Rotation.z }
end
