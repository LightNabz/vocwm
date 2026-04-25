# vocwm — Blueprint & Design Document
> Vertically Oriented Cyclic Window Manager

---

## 1. What is vocwm?

vocwm is a Wayland compositor built on **wlroots**, written in **C/C++**, configured via **Lua**. It is designed around a single core philosophy: workspaces exist in **2D space**, not a flat list.

The primary goal for v0.1: **workspaces logic works.**

---

## 2. Tech Stack

| Layer | Choice | Reason |
|---|---|---|
| Compositor backbone | wlroots | Battle-tested, sane, not raw libwayland hell |
| Core language | C / C++ | Conservative, performant, fits the domain |
| Config & scripting | Lua 5.4 (embedded) | Readable, programmable, proven in WM configs (AwesomeWM) |
| Build system | TBD (make / meson) | TBD |

> **Why not raw libwayland?**
> Once a wise man said: *"You should touch grass."* wlroots is the move.

> **Why not Rust/Zig/etc?**
> Familiarity wins. Brain calories go to compositor logic, not new language syntax.

---

## 3. The Workspace System

### 3.1 Grid Layout (Column-Major)

Workspaces are arranged in a **column-major 2D grid** of width `a` (rows per column).

- `a` = number of rows per column (default: 2, minimum: 2, must be integer)
- `b` = number of columns (auto-calculated from active workspace count, minimum: 1)
- Workspace index starts at **1**

**Index → Grid Position:**
```
row = (index - 1) % a        // 0-based row
col = (index - 1) / a        // 0-based col (integer division)
```

**Grid Position → Index:**
```
index = col * a + row + 1
```

**Example: a = 2**
```
[1][3][5]
[2][4][6]...
```

**Example: a = 3**
```
[1][4][7]
[2][5][8]
[3][6][9]...
```

### 3.2 What "Active" Means

An **active workspace** is one that:
- Has at least one window open, OR
- Is the workspace the user is currently on

> Active does NOT mean "every workspace between index 1 and the highest visited." Gaps are allowed.

---

## 4. Navigation Modes

vocwm exposes **two navigation modes**, each bindable to any key.

### 4.1 Snake / Cyclic Mode

Linear traversal through workspaces in index order.

```
next = (current % n) + 1
prev = ((current - 2 + n) % n) + 1
```

Where `n` = total active workspace count (bounded) or ever-growing (infinite).

### 4.2 Geometry Mode

Spatial navigation using the 2D grid.

```
RIGHT → (row, col + 1)
LEFT  → (row, col - 1)
DOWN  → (row + 1, col)
UP    → (row - 1, col)
```

Convert result back to index: `col * a + row + 1`

---

## 5. Generation Modes

Each navigation keybind operates in one of two generation modes:

### 5.1 Bounded Mode

Only cycles through **currently active** workspaces. Hitting a boundary or inactive cell falls back to the nearest valid workspace.

**Fallback rules (geometry bounded):**
- Going DOWN hits inactive/nonexistent cell → fall back to `row 0` of same column (always exists per R-rule)
- Going RIGHT hits inactive/nonexistent cell → fall back to `col 0` of same row

**The R-Rule:** Column 0 is always fully populated up to `a` rows. It is the eternal fallback anchor.

**Example (a=3, 5 active workspaces):**
```
[1][4]
[2][5]  ← going DOWN from [5]
[3]...
```
- [4] active → goes to [4] ✅
- [4] inactive → falls back to [1] (row 0, same col) ✅

### 5.2 Infinite Mode

Going past the boundary of active workspaces **generates a new workspace**.

**Example (a=3):**
```
[1][4]
[2][5]  ← going DOWN from [5]
[3][6]  ← [6] is created
```

Going RIGHT from [5] would create [8] (skipping [6] and [7] if they don't exist yet).

---

## 6. Config & Scripting (Lua)

vocwm does **not** hardcode navigation math. It exposes grid primitives and lets the user define behavior in Lua.

### 6.1 Exposed Variables

```lua
vocwm.current_workspace   -- current workspace index
vocwm.active_workspaces   -- table of active workspace indices
vocwm.grid_width          -- a (rows per column)
vocwm.row                 -- current row (0-based)
vocwm.col                 -- current col (0-based)
vocwm.total_cols          -- how many columns currently exist
```

### 6.2 Exposed Functions

```lua
vocwm.goto(index)                    -- switch to workspace by index
vocwm.get_workspace(row, col)        -- get index at grid position
vocwm.is_active(index)               -- check if workspace is active
vocwm.create_workspace()             -- spawn a new workspace (infinite mode)
```

### 6.3 Example Config

```lua
-- Geometry DOWN, bounded
bind("SUPER+J", function()
  local target = vocwm.get_workspace(vocwm.row + 1, vocwm.col)
  if vocwm.is_active(target) then
    vocwm.goto(target)
  else
    vocwm.goto(vocwm.get_workspace(0, vocwm.col))
  end
end)

-- Snake NEXT, infinite
bind("SUPER+N", function()
  local n = #vocwm.active_workspaces
  local next = (vocwm.current_workspace % n) + 1
  vocwm.goto(next)
end)
```

> vocwm's job: expose primitives. The math logic is yours 🙏

---

## 7. Development Roadmap

### Phase 1 — Bare Compositor
- [ ] wlroots setup, basic window rendering
- [ ] XDG shell support (normal windows)
- [ ] Keyboard input handling

### Phase 2 — Workspace Engine
- [ ] Grid state management (a, b, active list)
- [ ] `vocwm.goto()` implementation
- [ ] Workspace create/destroy

### Phase 3 — Lua Integration
- [ ] Embed Lua 5.4
- [ ] Expose all grid variables and functions
- [ ] Load and execute user config on startup

### Phase 4 — Navigation
- [ ] Keybind system
- [ ] Snake mode (bounded + infinite)
- [ ] Geometry mode (bounded + infinite)
- [ ] R-rule fallback logic

### Phase 5 — Daily Driving Test
- [ ] I myself being able to daily drive it

---

## 8. Non-Goals (for now)

- Animations (🥀)
- Fancy blur / eye candy
- Multi-monitor (maybe later)

---

*vocwm — we added a dimension. you're welcome.*