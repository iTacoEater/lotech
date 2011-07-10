function clone(table)
    local copy = {}
    for k, v in pairs(table) do
        copy[k] = v
    end
    return copy
end

function lt.ClearFuncs()
    lt.Advance = nil
    lt.Render = nil
    lt.PointerDown = nil
    lt.PointerUp = nil
    lt.PointerMove = nil
    lt.KeyUp = nil
    lt.KeyDown = nil
end

function lt.AddSpriteFiles(tab, name, num_frames)
    for i = 1, num_frames do
        table.insert(tab, string.format("%s_%04d", name, i))
    end
    return tab
end

function lt.MatchFields(tab, pattern)
    local fields = {}
    local values = {}
    for field, value in pairs(tab) do
        if type(field) == "string" then
            if string.match(field, pattern) then
                table.insert(fields, field)
            end
        end
    end
    table.sort(fields)
    for i, field in ipairs(fields) do
        values[i] = tab[field]
    end
    return values
end

function lt.Store(key, val)
    local t = type(val)
    if t == "string" or t == "number" or t == "boolean" or t == "nil" then
        local state = lt.state
        if not state then
            state = {}
            lt.state = state
        end
        state[key] = val
    else
        log("tried to store a value of type " .. t .. " for key " .. key)
    end
end

function lt.Retrieve(key)
    local state = lt.state
    if state then
        return state[key]
    else
        return nil
    end
end
