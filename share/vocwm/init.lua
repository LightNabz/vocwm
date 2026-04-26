-- vocwm/init.lua
-- Shipped default configuration.
-- Users override this by placing ~/.config/vocwm/init.lua
-- Everything here can be overridden — this file runs FIRST,
-- then init.lua runs on top of it.

-- ============================================================
-- Appearance
-- ============================================================

vocwm.set_master_ratio(0.55)

-- outer_gap: space between windows and screen edges
vocwm.set_outer_gap(10)

-- inner_gap: space between windows
vocwm.set_inner_gap(6)

-- border_width, then inactive color (RGBA 0.0–1.0)
vocwm.set_border(2, 0.22, 0.22, 0.28, 1.0)

-- focused window border color
vocwm.set_border_color_focused(0.45, 0.30, 0.85, 1.0)

-- ============================================================
-- Keybinds
-- format: "Super+key", "Super+Shift+key", "Super+Ctrl+key"
-- key names follow xkb: Return, space, h, j, k, l, 1..9, etc.
-- ============================================================

local terminal = "foot"      -- change to kitty/alacritty/etc.
local launcher = "fuzzel"    -- dmenu-like launcher

-- Launch terminal
vocwm.bind("Super+Return", function()
    vocwm.exec(terminal)
end)

-- Launch application launcher
vocwm.bind("Super+d", function()
    vocwm.exec(launcher)
end)

-- Close focused window
vocwm.bind("Super+q", function()
    vocwm.close_focused()
end)

-- Toggle floating for focused window
vocwm.bind("Super+f", function()
    vocwm.set_floating()   -- no arg = toggle
end)

-- Adjust master ratio
vocwm.bind("Super+l", function()
    local r = vocwm.get_master_ratio()
    vocwm.set_master_ratio(r + 0.05)
end)

vocwm.bind("Super+h", function()
    local r = vocwm.get_master_ratio()
    vocwm.set_master_ratio(r - 0.05)
end)

-- Grow / shrink gaps live
vocwm.bind("Super+equal", function()
    vocwm.set_inner_gap(vocwm.get_inner_gap() + 2)
    vocwm.set_outer_gap(vocwm.get_outer_gap() + 2)
end)

vocwm.bind("Super+minus", function()
    vocwm.set_inner_gap(math.max(0, vocwm.get_inner_gap() - 2))
    vocwm.set_outer_gap(math.max(0, vocwm.get_outer_gap() - 2))
end)

-- Screenshots (if grim is installed)
vocwm.bind("Super+Shift+s", function()
    vocwm.exec("grim ~/screenshot-$(date +%s).png")
end)

-- ============================================================
-- Example: user-specific app launchers
-- (uncomment / copy to init.lua to override)
-- ============================================================

-- vocwm.bind("Super+b", function() vocwm.exec("firefox") end)
-- vocwm.bind("Super+e", function() vocwm.exec("thunar") end)