#include "vocwm.h"
#include <unistd.h>
#include <sys/types.h>

extern "C" {
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
    #include <wlr/types/wlr_xdg_shell.h>
    #include <wlr/types/wlr_scene.h>
}

// ============================================================
// Internal helpers
// ============================================================

static inline VocwmServer *get_server(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "vocwm_server");
    VocwmServer *server = (VocwmServer *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return server;
}

static VocwmToplevel *get_focused_toplevel(VocwmServer *server) {
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    if (!focused) return NULL;
    VocwmToplevel *t;
    wl_list_for_each(t, &server->toplevels, link) {
        if (t->xdg_toplevel->base->surface == focused)
            return t;
    }
    return NULL;
}

// ============================================================
// Lua-exposed functions
// ============================================================

static int lua_set_master_ratio(lua_State *L) {
    float r = (float)luaL_checknumber(L, 1);
    r = r < 0.1f ? 0.1f : r > 0.9f ? 0.9f : r;
    VocwmServer *s = get_server(L);
    s->master_ratio = r;
    arrange_toplevels(s);
    return 0;
}

static int lua_get_master_ratio(lua_State *L) {
    lua_pushnumber(L, get_server(L)->master_ratio);
    return 1;
}

static int lua_set_outer_gap(lua_State *L) {
    int g = (int)luaL_checkinteger(L, 1);
    g = g < 0 ? 0 : g > 256 ? 256 : g;
    VocwmServer *s = get_server(L);
    s->outer_gap = g;
    arrange_toplevels(s);
    return 0;
}

static int lua_set_inner_gap(lua_State *L) {
    int g = (int)luaL_checkinteger(L, 1);
    g = g < 0 ? 0 : g > 256 ? 256 : g;
    VocwmServer *s = get_server(L);
    s->inner_gap = g;
    arrange_toplevels(s);
    return 0;
}

static int lua_get_outer_gap(lua_State *L) {
    lua_pushinteger(L, get_server(L)->outer_gap);
    return 1;
}

static int lua_get_inner_gap(lua_State *L) {
    lua_pushinteger(L, get_server(L)->inner_gap);
    return 1;
}

// vocwm.set_border(width [, r, g, b, a])
static int lua_set_border(lua_State *L) {
    VocwmServer *s = get_server(L);
    s->border_width = (int)luaL_checkinteger(L, 1);
    s->border_width = s->border_width < 0 ? 0 : s->border_width > 32 ? 32 : s->border_width;
    if (lua_gettop(L) >= 5) {
        s->border_color[0] = (float)luaL_checknumber(L, 2);
        s->border_color[1] = (float)luaL_checknumber(L, 3);
        s->border_color[2] = (float)luaL_checknumber(L, 4);
        s->border_color[3] = (float)luaL_checknumber(L, 5);
    }
    arrange_toplevels(s);
    return 0;
}

// vocwm.set_border_color(r, g, b, a)        — inactive
// vocwm.set_border_color_focused(r, g, b, a) — focused
static int lua_set_border_color(lua_State *L) {
    VocwmServer *s = get_server(L);
    s->border_color[0] = (float)luaL_checknumber(L, 1);
    s->border_color[1] = (float)luaL_checknumber(L, 2);
    s->border_color[2] = (float)luaL_checknumber(L, 3);
    s->border_color[3] = (float)luaL_checknumber(L, 4);
    update_borders(s);
    return 0;
}

static int lua_set_border_color_focused(lua_State *L) {
    VocwmServer *s = get_server(L);
    s->border_color_focused[0] = (float)luaL_checknumber(L, 1);
    s->border_color_focused[1] = (float)luaL_checknumber(L, 2);
    s->border_color_focused[2] = (float)luaL_checknumber(L, 3);
    s->border_color_focused[3] = (float)luaL_checknumber(L, 4);
    update_borders(s);
    return 0;
}

// vocwm.set_floating([bool]) — toggle or set floating for focused window
static int lua_set_floating(lua_State *L) {
    VocwmServer *s = get_server(L);
    VocwmToplevel *t = get_focused_toplevel(s);
    if (!t) return 0;
    if (lua_gettop(L) >= 1)
        t->floating = lua_toboolean(L, 1);
    else
        t->floating = !t->floating;
    arrange_toplevels(s);
    return 0;
}

// vocwm.close_focused() — close the focused window
static int lua_close_focused(lua_State *L) {
    VocwmServer *s = get_server(L);
    VocwmToplevel *t = get_focused_toplevel(s);
    if (t) wlr_xdg_toplevel_send_close(t->xdg_toplevel);
    return 0;
}

// vocwm.exec(cmd) — spawn a process
static int lua_exec(lua_State *L) {
    const char *cmd = luaL_checkstring(L, 1);
    if (fork() == 0) {
        // child: detach from WM's process group, exec via shell
        setsid();
        execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
    return 0;
}

// ============================================================
// Keybind registry
//
// bindings are stored as a Lua table in the registry:
//   registry["vocwm_bindings"] = {
//     ["Super+Return"] = function() ... end,
//     ...
//   }
//
// vocwm.bind(key_string, fn) — register a keybind
// key_string format: "Super+Shift+t", "Super+Return", etc.
// ============================================================

static int lua_bind(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // get or create the bindings table
    lua_getfield(L, LUA_REGISTRYINDEX, "vocwm_bindings");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "vocwm_bindings");
    }
    // bindings[key] = fn
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
    return 0;
}

// Called from keyboard.cpp to dispatch a key combo string
// Returns true if a binding was found and called
bool lua_dispatch_binding(VocwmServer *server, const char *combo) {
    lua_State *L = server->lua;
    lua_getfield(L, LUA_REGISTRYINDEX, "vocwm_bindings");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return false; }

    lua_getfield(L, -1, combo);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    int err = lua_pcall(L, 0, 0, 0);
    if (err != LUA_OK) {
        wlr_log(WLR_ERROR, "keybind error [%s]: %s", combo,
            lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // pop bindings table
    return true;
}

// ============================================================
// Lua init
// ============================================================

void lua_init(VocwmServer *server) {
    server->lua = luaL_newstate();
    luaL_openlibs(server->lua);

    lua_pushlightuserdata(server->lua, server);
    lua_setfield(server->lua, LUA_REGISTRYINDEX, "vocwm_server");

    lua_newtable(server->lua);

#define REG(name, fn) \
    lua_pushcfunction(server->lua, fn); \
    lua_setfield(server->lua, -2, name)

    REG("set_master_ratio",        lua_set_master_ratio);
    REG("get_master_ratio",        lua_get_master_ratio);
    REG("set_outer_gap",           lua_set_outer_gap);
    REG("set_inner_gap",           lua_set_inner_gap);
    REG("get_outer_gap",           lua_get_outer_gap);
    REG("get_inner_gap",           lua_get_inner_gap);
    REG("set_border",              lua_set_border);
    REG("set_border_color",        lua_set_border_color);
    REG("set_border_color_focused",lua_set_border_color_focused);
    REG("set_floating",            lua_set_floating);
    REG("close_focused",           lua_close_focused);
    REG("exec",                    lua_exec);
    REG("bind",                    lua_bind);

#undef REG

    lua_setglobal(server->lua, "vocwm");

    // 1. Load built-in default.lua (shipped with vocwm, always runs first)
    //    We embed it as a C string so there's no external file dependency.
    //    See default.lua for the actual content — generated at build time or
    //    shipped alongside the binary. For now we load from the install prefix.
    const char *prefix = getenv("VOCWM_DATA_DIR");
    if (!prefix) prefix = "/usr/local/share/vocwm";

    char default_path[512];
    snprintf(default_path, sizeof(default_path), "%s/init.lua", prefix);
    int err = luaL_dofile(server->lua, default_path);
    if (err != LUA_OK) {
        wlr_log(WLR_ERROR, "Failed to load default config (%s): %s",
            default_path, lua_tostring(server->lua, -1));
        lua_pop(server->lua, 1);
        // set sane C-level fallbacks so the compositor doesn't explode
        server->master_ratio = 0.55f;
        server->outer_gap    = 8;
        server->inner_gap    = 6;
        server->border_width = 2;
        server->border_color[0] = 0.25f; server->border_color[1] = 0.25f;
        server->border_color[2] = 0.25f; server->border_color[3] = 1.0f;
        server->border_color_focused[0] = 0.5f;
        server->border_color_focused[1] = 0.3f;
        server->border_color_focused[2] = 0.9f;
        server->border_color_focused[3] = 1.0f;
    } else {
        wlr_log(WLR_INFO, "Loaded default config from %s", default_path);
    }

    // 2. Load user config — optional, overrides defaults
    const char *home = getenv("HOME");
    if (home) {
        char user_path[512];
        snprintf(user_path, sizeof(user_path),
            "%s/.config/vocwm/init.lua", home);
        err = luaL_dofile(server->lua, user_path);
        if (err != LUA_OK) {
            wlr_log(WLR_INFO, "User config not loaded (%s): %s",
                user_path, lua_tostring(server->lua, -1));
            lua_pop(server->lua, 1);
        } else {
            wlr_log(WLR_INFO, "Loaded user config from %s", user_path);
        }
    }
}

// ============================================================
// Border management
//
// Fix for the focus-race bug: update_borders takes an explicit
// "focused" pointer instead of re-querying seat state, because
// this is called right after the seat state changes so the
// keyboard_state.focused_surface is already the NEW surface.
// Pass NULL to just re-read seat state (safe after settle).
// ============================================================

static bool surface_is_focused(VocwmToplevel *t, VocwmToplevel *focused_hint) {
    if (focused_hint) return t == focused_hint;
    struct wlr_surface *f = t->server->seat->keyboard_state.focused_surface;
    return f && t->xdg_toplevel->base->surface == f;
}

static void apply_border(VocwmToplevel *t, int x, int y, int w, int h,
                         VocwmToplevel *focused_hint) {
    VocwmServer *s = t->server;
    int bw = s->border_width;

    if (bw <= 0) {
        if (t->border_rect) {
            wlr_scene_node_destroy(&t->border_rect->node);
            t->border_rect = NULL;
        }
        return;
    }

    const float *color = surface_is_focused(t, focused_hint)
        ? s->border_color_focused
        : s->border_color;

    int bx = x - bw, by = y - bw;
    int bw2 = w + bw * 2, bh2 = h + bw * 2;

    if (!t->border_rect) {
        t->border_rect = wlr_scene_rect_create(
            &s->scene->tree, bw2, bh2, color);
    } else {
        wlr_scene_rect_set_size(t->border_rect, bw2, bh2);
        wlr_scene_rect_set_color(t->border_rect, color);
    }

    wlr_scene_node_set_position(&t->border_rect->node, bx, by);
    wlr_scene_node_lower_to_bottom(&t->border_rect->node);
}

// Public: refresh border colors only (called after focus changes)
// focused_hint = the toplevel that just received focus
void update_borders(VocwmServer *server) {
    VocwmToplevel *focused = get_focused_toplevel(server);
    VocwmToplevel *t;
    wl_list_for_each(t, &server->toplevels, link) {
        if (!t->border_rect) continue;
        const float *color = (t == focused)
            ? server->border_color_focused
            : server->border_color;
        wlr_scene_rect_set_color(t->border_rect, color);
    }
}

// ============================================================
// Dwindle tiling engine
//
// Classic dwindle: each new window splits the remaining space
// in alternating directions (horizontal → vertical → horizontal…)
// so windows tile like a Fibonacci spiral rather than a plain
// master-stack column.
//
// With 1 window:
//   ┌──────────────┐
//   │      1       │
//   └──────────────┘
//
// With 2 windows (first split horizontal, master on left):
//   ┌───────┬──────┐
//   │   1   │  2   │
//   └───────┴──────┘
//
// With 3 windows (second split vertical inside right half):
//   ┌───────┬──────┐
//   │   1   │  2   │
//   │       ├──────┤
//   │       │  3   │
//   └───────┴──────┘
//
// With 4 windows:
//   ┌───────┬──────┐
//   │   1   │  2   │
//   │       ├───┬──┤
//   │       │ 3 │ 4│
//   └───────┴───┴──┘
//
// master_ratio controls the first split only.
// Subsequent splits are always 50/50.
// ============================================================

static void dwindle_place(VocwmToplevel **wins, int n,
                          int x, int y, int w, int h,
                          int depth,
                          VocwmServer *server,
                          VocwmToplevel *focused_hint) {
    int ig = server->inner_gap;

    if (n == 1) {
        wlr_scene_node_set_position(&wins[0]->scene_tree->node, x, y);
        wlr_xdg_toplevel_set_size(wins[0]->xdg_toplevel, w, h);
        apply_border(wins[0], x, y, w, h, focused_hint);
        wins[0]->x = x; wins[0]->y = y;
        wins[0]->width = w; wins[0]->height = h;
        return;
    }

    // first split uses master_ratio, subsequent splits 50/50
    float ratio = (depth == 0) ? server->master_ratio : 0.5f;

    // alternate split direction: even depth = split horizontally (left|right)
    //                            odd depth  = split vertically   (top|bottom)
    bool horiz = (depth % 2 == 0);

    int half_gap = ig / 2;

    if (horiz) {
        int left_w = (int)(w * ratio) - half_gap;
        int right_x = x + left_w + ig;
        int right_w = w - left_w - ig;

        // first window takes left slice
        dwindle_place(wins, 1, x, y, left_w, h, depth + 1, server, focused_hint);
        // rest recurse into right slice
        dwindle_place(wins + 1, n - 1, right_x, y, right_w, h, depth + 1, server, focused_hint);
    } else {
        int top_h = (int)(h * ratio) - half_gap;
        int bot_y = y + top_h + ig;
        int bot_h = h - top_h - ig;

        dwindle_place(wins, 1, x, y, w, top_h, depth + 1, server, focused_hint);
        dwindle_place(wins + 1, n - 1, x, bot_y, w, bot_h, depth + 1, server, focused_hint);
    }
}

void arrange_toplevels(VocwmServer *server) {
    if (wl_list_empty(&server->outputs)) return;

    // use usable area (set by arrange_layers, falls back to full output)
    int ax = server->usable_x;
    int ay = server->usable_y;
    int aw = server->usable_w;
    int ah = server->usable_h;

    // if usable area not yet set, derive from first output
    if (aw == 0 || ah == 0) {
        VocwmOutput *out = wl_container_of(server->outputs.next, out, link);
        wlr_output_effective_resolution(out->wlr_output, &aw, &ah);
        ax = 0; ay = 0;
    }

    int og = server->outer_gap;

    // shrink by outer gap
    int tx = ax + og;
    int ty = ay + og;
    int tw = aw - og * 2;
    int th = ah - og * 2;

    if (tw <= 0 || th <= 0) return;

    // collect tiling windows
    VocwmToplevel *tiling[256];
    int count = 0;
    VocwmToplevel *t;
    wl_list_for_each(t, &server->toplevels, link) {
        if (!t->floating && count < 256)
            tiling[count++] = t;
    }

    if (count == 0) return;

    // snapshot focused toplevel NOW before any size/position changes
    // (this is the fix: we capture the pointer once, pass it through,
    //  no re-querying seat state mid-arrange)
    VocwmToplevel *focused = get_focused_toplevel(server);

    dwindle_place(tiling, count, tx, ty, tw, th, 0, server, focused);

    wlr_log(WLR_DEBUG, "dwindle: %d windows in [%d,%d %dx%d] og=%d ig=%d",
        count, tx, ty, tw, th, og, server->inner_gap);
}