/*
 * luaa.c - Lua configuration management
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "common/util.h"

#include <ev.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <xcb/xcb.h>
#include <xcb/xtest.h>

#include <basedir_fs.h>

#include "awesome.h"
#include "awesome-version-internal.h"
#include "ewmh.h"
#include "config.h"
#include "luaa.h"
#include "spawn.h"
#include "tag.h"
#include "client.h"
#include "screen.h"
#include "event.h"
#include "mouse.h"
#include "selection.h"
#include "window.h"
#include "common/xcursor.h"

extern const struct luaL_reg awesome_hooks_lib[];
extern const struct luaL_reg awesome_dbus_lib[];
extern const struct luaL_reg awesome_keygrabber_lib[];
extern const struct luaL_reg awesome_mousegrabber_lib[];
extern const struct luaL_reg awesome_button_methods[];
extern const struct luaL_reg awesome_button_meta[];
extern const struct luaL_reg awesome_image_methods[];
extern const struct luaL_reg awesome_image_meta[];
extern const struct luaL_reg awesome_mouse_methods[];
extern const struct luaL_reg awesome_mouse_meta[];
extern const struct luaL_reg awesome_screen_methods[];
extern const struct luaL_reg awesome_screen_meta[];
extern const struct luaL_reg awesome_client_methods[];
extern const struct luaL_reg awesome_client_meta[];
extern const struct luaL_reg awesome_tag_methods[];
extern const struct luaL_reg awesome_tag_meta[];
extern const struct luaL_reg awesome_widget_methods[];
extern const struct luaL_reg awesome_widget_meta[];
extern const struct luaL_reg awesome_wibox_methods[];
extern const struct luaL_reg awesome_wibox_meta[];
extern const struct luaL_reg awesome_key_methods[];
extern const struct luaL_reg awesome_key_meta[];

/** Send fake events. Usually the current focused client will get it.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 * \luastack
 * \lvalue A client.
 * \lparam The event type: key_press, key_release, button_press, button_release
 * or motion_notify.
 * \lparam The detail: in case of a key event, this is the keycode to send, in
 * case of a button event this is the number of the button. In case of a motion
 * event, this is a boolean value which if true make the coordinates relatives.
 * \lparam In case of a motion event, this is the X coordinate.
 * \lparam In case of a motion event, this is the Y coordinate.
 * \lparam In case of a motion event, this is the screen number to move on.
 * If not specified, the current one is used.
 */
static int
luaA_root_fake_input(lua_State *L)
{
    if(!globalconf.have_xtest)
    {
        luaA_warn(L, "XTest extension is not available, cannot fake input.");
        return 0;
    }

    size_t tlen;
    const char *stype = luaL_checklstring(L, 2, &tlen);
    uint8_t type, detail;
    int x = 0, y = 0;
    xcb_window_t root = XCB_NONE;

    switch(a_tokenize(stype, tlen))
    {
      case A_TK_KEY_PRESS:
        type = XCB_KEY_PRESS;
        detail = luaL_checknumber(L, 3); /* keycode */
        break;
      case A_TK_KEY_RELEASE:
        type = XCB_KEY_RELEASE;
        detail = luaL_checknumber(L, 3); /* keycode */
        break;
      case A_TK_BUTTON_PRESS:
        type = XCB_BUTTON_PRESS;
        detail = luaL_checknumber(L, 3); /* button number */
        break;
      case A_TK_BUTTON_RELEASE:
        type = XCB_BUTTON_RELEASE;
        detail = luaL_checknumber(L, 3); /* button number */
        break;
      case A_TK_MOTION_NOTIFY:
        type = XCB_MOTION_NOTIFY;
        detail = luaA_checkboolean(L, 3); /* relative to the current position or not */
        x = luaL_checknumber(L, 4);
        y = luaL_checknumber(L, 5);
        if(lua_gettop(L) == 6 && !globalconf.xinerama_is_active)
        {
            int screen = luaL_checknumber(L, 6);
            luaA_checkscreen(screen);
            root = xutil_screen_get(globalconf.connection, screen)->root;
        }
        break;
      default:
        return 0;
    }

    xcb_test_fake_input(globalconf.connection,
                        type,
                        detail,
                        XCB_CURRENT_TIME,
                        root,
                        x, y,
                        0);
    return 0;
}

/** Get or set global key bindings.
 * This binding will be available when you'll press keys on root window.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 * \luastack
 * \lparam An array of key bindings objects, or nothing.
 * \lreturn The array of key bindings objects of this client.
 */
static int
luaA_root_keys(lua_State *L)
{
    if(lua_gettop(L) == 1)
    {
        luaA_key_array_set(L, 1, &globalconf.keys);

        int nscreen = xcb_setup_roots_length(xcb_get_setup(globalconf.connection));

        for(int phys_screen = 0; phys_screen < nscreen; phys_screen++)
        {
            xcb_screen_t *s = xutil_screen_get(globalconf.connection, phys_screen);
            xcb_ungrab_key(globalconf.connection, XCB_GRAB_ANY, s->root, XCB_BUTTON_MASK_ANY);
            window_grabkeys(s->root, &globalconf.keys);
        }
    }

    return luaA_key_array_get(L, &globalconf.keys);
}

/** Get or set global mouse bindings.
 * This binding will be available when you'll click on root window.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 * \luastack
 * \lparam An array of mouse button bindings objects, or nothing.
 * \lreturn The array of mouse button bindings objects.
 */
static int
luaA_root_buttons(lua_State *L)
{
    if(lua_gettop(L) == 1)
        luaA_button_array_set(L, 1, &globalconf.buttons);

    return luaA_button_array_get(L, &globalconf.buttons);
}

/** Set the root cursor.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 * \luastack
 * \lparam A X cursor name.
 */
static int
luaA_root_cursor(lua_State *L)
{
    const char *cursor_name = luaL_checkstring(L, 1);
    uint16_t cursor_font = xcursor_font_fromstr(cursor_name);

    if(cursor_font)
    {
        uint32_t change_win_vals[] = { xcursor_new(globalconf.connection, cursor_font) };

        for(int screen_nbr = 0;
            screen_nbr < xcb_setup_roots_length(xcb_get_setup(globalconf.connection));
            screen_nbr++)
            xcb_change_window_attributes(globalconf.connection,
                                         xutil_screen_get(globalconf.connection, screen_nbr)->root,
                                         XCB_CW_CURSOR,
                                         change_win_vals);
    }
    else
        luaA_warn(L, "invalid cursor %s", cursor_name);

    return 0;
}

/** Quit awesome.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_quit(lua_State *L __attribute__ ((unused)))
{
    ev_unloop(globalconf.loop, 1);
    return 0;
}

/** Execute another application, probably a window manager, to replace
 * awesome.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam The command line to execute.
 */
static int
luaA_exec(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);

    awesome_atexit();

    a_exec(cmd);
    return 0;
}

/** Restart awesome.
 */
static int
luaA_restart(lua_State *L __attribute__ ((unused)))
{
    awesome_restart();
    return 0;
}

static void
luaA_openlib(lua_State *L, const char *name,
             const struct luaL_reg methods[],
             const struct luaL_reg meta[])
{
    luaL_newmetatable(L, name);                                        /* 1 */
    lua_pushvalue(L, -1);           /* dup metatable                      2 */
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable      1 */

    luaL_register(L, NULL, meta);                                      /* 1 */
    luaL_register(L, name, methods);                                   /* 2 */
    lua_pushvalue(L, -1);           /* dup self as metatable              3 */
    lua_setmetatable(L, -2);        /* set self as metatable              2 */
    lua_pop(L, 2);
}

/** UTF-8 aware string length computing.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_mbstrlen(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    lua_pushnumber(L, (ssize_t) mbstowcs(NULL, NONULL(cmd), 0));
    return 1;
}

/** Overload standard Lua next function to use __next key on metatable.
 * \param L The Lua VM state.
 * \param The number of elements pushed on stack.
 */
static int
luaAe_next(lua_State *L)
{
    if(luaL_getmetafield(L, 1, "__next"))
    {
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);
    if(lua_next(L, 1))
        return 2;
    lua_pushnil(L);
    return 1;
}

/** Overload lua_next() function by using __next metatable field
 * to get next elements.
 * \param L The Lua VM stack.
 * \param idx The index number of elements in stack.
 * \return 1 if more elements to come, 0 otherwise.
 */
int
luaA_next(lua_State *L, int idx)
{
    if(luaL_getmetafield(L, idx, "__next"))
    {
        /* if idx is relative, reduce it since we got __next */
        if(idx < 0) idx--;
        /* copy table and then move key */
        lua_pushvalue(L, idx);
        lua_pushvalue(L, -3);
        lua_remove(L, -4);
        lua_pcall(L, 2, 2, 0);
        /* next returned nil, it's the end */
        if(lua_isnil(L, -1))
        {
            /* remove nil */
            lua_pop(L, 2);
            return 0;
        }
        return 1;
    }
    else if(lua_istable(L, idx))
        return lua_next(L, idx);
    /* remove the key */
    lua_pop(L, 1);
    return 0;
}

/** Generic pairs function.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_generic_pairs(lua_State *L)
{
    lua_pushvalue(L, lua_upvalueindex(1));  /* return generator, */
    lua_pushvalue(L, 1);  /* state, */
    lua_pushnil(L);  /* and initial value */
    return 3;
}

/** Overload standard pairs function to use __pairs field of metatables.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaAe_pairs(lua_State *L)
{
    if(luaL_getmetafield(L, 1, "__pairs"))
    {
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    luaL_checktype(L, 1, LUA_TTABLE);
    return luaA_generic_pairs(L);
}

static int
luaA_ipairs_aux(lua_State *L)
{
    int i = luaL_checkint(L, 2) + 1;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushinteger(L, i);
    lua_rawgeti(L, 1, i);
    return (lua_isnil(L, -1)) ? 0 : 2;
}

/** Overload standard ipairs function to use __ipairs field of metatables.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaAe_ipairs(lua_State *L)
{
    if(luaL_getmetafield(L, 1, "__ipairs"))
    {
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);  /* and initial value */
    return 3;
}

/** Enhanced type() function which recognize awesome objects.
 * \param L The Lua VM state.
 * \return The number of arguments pushed on the stack.
 */
static int
luaAe_type(lua_State *L)
{
    luaL_checkany(L, 1);
#define CHECK_TYPE(type) \
    do { \
        if(luaA_toudata(L, 1, #type)) \
        { \
            lua_pushliteral(L, #type); \
            return 1; \
        } \
    } while(0)
CHECK_TYPE(wibox);
CHECK_TYPE(client);
CHECK_TYPE(image);
CHECK_TYPE(key);
CHECK_TYPE(button);
CHECK_TYPE(tag);
CHECK_TYPE(widget);
#undef CHECK_TYPE
    lua_pushstring(L, luaL_typename(L, 1));
    return 1;
}

/** Replace various standards Lua functions with our own.
 * \param L The Lua VM state.
 */
static void
luaA_fixups(lua_State *L)
{
    /* export string.wlen */
    lua_getglobal(L, "string");
    lua_pushcfunction(L, luaA_mbstrlen);
    lua_setfield(L, -2, "wlen");
    lua_pop(L, 1);
    /* replace next */
    lua_pushliteral(L, "next");
    lua_pushcfunction(L, luaAe_next);
    lua_settable(L, LUA_GLOBALSINDEX);
    /* replace pairs */
    lua_pushliteral(L, "pairs");
    lua_pushcfunction(L, luaAe_next);
    lua_pushcclosure(L, luaAe_pairs, 1); /* pairs get next as upvalue */
    lua_settable(L, LUA_GLOBALSINDEX);
    /* replace ipairs */
    lua_pushliteral(L, "ipairs");
    lua_pushcfunction(L, luaA_ipairs_aux);
    lua_pushcclosure(L, luaAe_ipairs, 1);
    lua_settable(L, LUA_GLOBALSINDEX);
    /* replace type */
    lua_pushliteral(L, "type");
    lua_pushcfunction(L, luaAe_type);
    lua_settable(L, LUA_GLOBALSINDEX);
    /* set selection */
    lua_pushliteral(L, "selection");
    lua_pushcfunction(L, luaA_selection_get);
    lua_settable(L, LUA_GLOBALSINDEX);
}

/** __next function for wtable objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wtable_next(lua_State *L)
{
    /* upvalue 1 is content table */
    if(lua_next(L, lua_upvalueindex(1)))
        return 2;
    lua_pushnil(L);
    return 1;
}

/** __ipairs function for wtable objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wtable_ipairs(lua_State *L)
{
    /* push ipairs_aux */
    lua_pushvalue(L, lua_upvalueindex(2));
    /* push content table */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushinteger(L, 0);  /* and initial value */
    return 3;
}

/** Index function of wtable objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wtable_index(lua_State *L)
{
    size_t len;
    const char *buf;

    lua_pushvalue(L, 2);
    /* check for size, waiting lua 5.2 and __len on tables */
    if((buf = lua_tolstring(L, -1, &len)))
        if(a_tokenize(buf, len) == A_TK_LEN)
        {
            lua_pushnumber(L, lua_objlen(L, lua_upvalueindex(1)));
            return 1;
        }
    lua_pop(L, 1);

    /* upvalue 1 is content table */
    lua_rawget(L, lua_upvalueindex(1));
    return 1;
}

/** Newndex function of wtable objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wtable_newindex(lua_State *L)
{
    bool invalid = false;

    /* push key on top */
    lua_pushvalue(L, 2);
    /* get current key value in content table */
    lua_rawget(L, lua_upvalueindex(1));
    /* if value is a widget, notify change */
    if(lua_istable(L, -1) || luaA_toudata(L, -1, "widget"))
        invalid = true;

    lua_pop(L, 1); /* remove value */

    /* if new value is a widget or a table */
    if(lua_istable(L, 3))
    {
        luaA_table2wtable(L);
        invalid = true;
    }
    else if(!invalid && luaA_toudata(L, 3, "widget"))
        invalid = true;

    /* upvalue 1 is content table */
    lua_rawset(L, lua_upvalueindex(1));

    if(invalid)
        luaA_wibox_invalidate_byitem(L, lua_topointer(L, 1));

    return 0;
}

/** Convert the top element of the stack to a proxied wtable.
 * \param L The Lua VM state.
 */
void
luaA_table2wtable(lua_State *L)
{
    if(!lua_istable(L, -1))
        return;

    lua_newtable(L); /* create *real* content table */
    lua_newtable(L); /* metatable */
    lua_pushvalue(L, -2); /* copy content table */
    lua_pushcfunction(L, luaA_ipairs_aux); /* push ipairs aux */
    lua_pushcclosure(L, luaA_wtable_ipairs, 2);
    lua_pushvalue(L, -3); /* copy content table */
    lua_pushcclosure(L, luaA_wtable_next, 1); /* __next has the content table as upvalue */
    lua_pushvalue(L, -4); /* copy content table */
    lua_pushcclosure(L, luaA_wtable_index, 1); /* __index has the content table as upvalue */
    lua_pushvalue(L, -5); /* copy content table */
    lua_pushcclosure(L, luaA_wtable_newindex, 1); /* __newindex has the content table as upvalue */
    /* set metatable field with just pushed closure */
    lua_setfield(L, -5, "__newindex");
    lua_setfield(L, -4, "__index");
    lua_setfield(L, -3, "__next");
    lua_setfield(L, -2, "__ipairs");
    /* set metatable impossible to touch */
    lua_pushliteral(L, "wtable");
    lua_setfield(L, -2, "__metatable");
    /* set new metatable on original table */
    lua_setmetatable(L, -3);

    /* initial key */
    lua_pushnil(L);
    /* go through original table */
    while(lua_next(L, -3))
    {
        /* if convert value to wtable */
        luaA_table2wtable(L);
        /* copy key */
        lua_pushvalue(L, -2);
        /* move key before value */
        lua_insert(L, -2);
        /* set same value in content table */
        lua_rawset(L, -4);
        /* copy key */
        lua_pushvalue(L, -1);
        /* push the new value :-> */
        lua_pushnil(L);
        /* set orig[k] = nil */
        lua_rawset(L, -5);
    }
    /* remove content table */
    lua_pop(L, 1);
}

/** Look for an item: table, function, etc.
 * \param L The Lua VM state.
 * \param item The pointer item.
 */
bool
luaA_hasitem(lua_State *L, const void *item)
{
    lua_pushnil(L);
    while(luaA_next(L, -2))
    {
        if(lua_topointer(L, -1) == item)
        {
            /* remove value and key */
            lua_pop(L, 2);
            return true;
        }
        if(lua_istable(L, -1))
            if(luaA_hasitem(L, item))
            {
                /* remove key and value */
                lua_pop(L, 2);
                return true;
            }
        /* remove value */
        lua_pop(L, 1);
    }
    return false;
}

/** Browse a table pushed on top of the index, and put all its table and
 * sub-table into an array.
 * \param L The Lua VM state.
 * \param elems The elements array.
 * \return False if we encounter an elements already in list.
 */
static bool
luaA_isloop_check(lua_State *L, void_array_t *elems)
{
    if(lua_istable(L, -1))
    {
        const void *object = lua_topointer(L, -1);

        /* Check that the object table is not already in the list */
        for(int i = 0; i < elems->len; i++)
            if(elems->tab[i] == object)
                return false;

        /* push the table in the elements list */
        void_array_append(elems, object);

        /* look every object in the "table" */
        lua_pushnil(L);
        while(luaA_next(L, -2))
        {
            if(!luaA_isloop_check(L, elems))
            {
                /* remove key and value */
                lua_pop(L, 2);
                return false;
            }
            /* remove value, keep key for next iteration */
            lua_pop(L, 1);
        }
    }
    return true;
}

/** Check if a table is a loop. When using tables as direct acyclic digram,
 * this is useful.
 * \param L The Lua VM state.
 * \param idx The index of the table in the stack
 * \return True if the table loops.
 */
bool
luaA_isloop(lua_State *L, int idx)
{
    /* elems is an elements array that we will fill with all array we
     * encounter while browsing the tables */
    void_array_t elems;

    void_array_init(&elems);

    /* push table on top */
    lua_pushvalue(L, idx);

    bool ret = luaA_isloop_check(L, &elems);

    /* remove pushed table */
    lua_pop(L, 1);

    void_array_wipe(&elems);

    return !ret;
}

/** awesome global table.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lfield font The default font.
 * \lfield conffile The configuration file which has been loaded.
 */
static int
luaA_awesome_index(lua_State *L)
{
    if(luaA_usemetatable(L, 1, 2))
        return 1;

    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    switch(a_tokenize(buf, len))
    {
      case A_TK_FONT:
        {
            char *font = pango_font_description_to_string(globalconf.font->desc);
            lua_pushstring(L, font);
            g_free(font);
        }
        break;
      case A_TK_CONFFILE:
        lua_pushstring(L, globalconf.conffile);
        break;
      case A_TK_FG:
        luaA_pushcolor(L, &globalconf.colors.fg);
        break;
      case A_TK_BG:
        luaA_pushcolor(L, &globalconf.colors.bg);
        break;
      default:
        return 0;
    }

    return 1;
}

/** Newindex function for the awesome global table.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_awesome_newindex(lua_State *L)
{
    if(luaA_usemetatable(L, 1, 2))
        return 1;

    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    switch(a_tokenize(buf, len))
    {
      case A_TK_FONT:
        {
            const char *newfont = luaL_checkstring(L, 3);
            draw_font_delete(&globalconf.font);
            globalconf.font = draw_font_new(newfont);
        }
        break;
      case A_TK_FG:
        if((buf = luaL_checklstring(L, 3, &len)))
           xcolor_init_reply(xcolor_init_unchecked(&globalconf.colors.fg, buf, len));
        break;
      case A_TK_BG:
        if((buf = luaL_checklstring(L, 3, &len)))
           xcolor_init_reply(xcolor_init_unchecked(&globalconf.colors.bg, buf, len));
        break;
      default:
        return 0;
    }

    return 0;
}

/** Initialize the Lua VM
 * \param xdg An xdg handle to use to get XDG basedir.
 */
void
luaA_init(xdgHandle xdg)
{
    lua_State *L;
    static const struct luaL_reg awesome_lib[] =
    {
        { "quit", luaA_quit },
        { "exec", luaA_exec },
        { "spawn", luaA_spawn },
        { "restart", luaA_restart },
        { "__index", luaA_awesome_index },
        { "__newindex", luaA_awesome_newindex },
        { NULL, NULL }
    };
    static const struct luaL_reg root_lib[] =
    {
        { "buttons", luaA_root_buttons },
        { "keys", luaA_root_keys },
        { "cursor", luaA_root_cursor },
        { "fake_input", luaA_root_fake_input },
        { NULL, NULL }
    };

    L = globalconf.L = luaL_newstate();

    luaL_openlibs(L);

    luaA_fixups(L);

    /* Export awesome lib */
    luaA_openlib(L, "awesome", awesome_lib, awesome_lib);

    /* Export root lib */
    luaA_openlib(L, "root", root_lib, root_lib);

    /* Export hooks lib */
    luaL_register(L, "hooks", awesome_hooks_lib);

    /* Export D-Bus lib */
    luaL_register(L, "dbus", awesome_dbus_lib);

    /* Export keygrabber lib */
    luaL_register(L, "keygrabber", awesome_keygrabber_lib);

    /* Export mousegrabber lib */
    luaL_register(L, "mousegrabber", awesome_mousegrabber_lib);

    /* Export screen */
    luaA_openlib(L, "screen", awesome_screen_methods, awesome_screen_meta);

    /* Export mouse */
    luaA_openlib(L, "mouse", awesome_mouse_methods, awesome_mouse_meta);

    /* Export button */
    luaA_openlib(L, "button", awesome_button_methods, awesome_button_meta);

    /* Export image */
    luaA_openlib(L, "image", awesome_image_methods, awesome_image_meta);

    /* Export tag */
    luaA_openlib(L, "tag", awesome_tag_methods, awesome_tag_meta);

    /* Export wibox */
    luaA_openlib(L, "wibox", awesome_wibox_methods, awesome_wibox_meta);

    /* Export widget */
    luaA_openlib(L, "widget", awesome_widget_methods, awesome_widget_meta);

    /* Export client */
    luaA_openlib(L, "client", awesome_client_methods, awesome_client_meta);

    /* Export keys */
    luaA_openlib(L, "key", awesome_key_methods, awesome_key_meta);

    lua_pushliteral(L, "AWESOME_VERSION");
    lua_pushstring(L, AWESOME_VERSION);
    lua_settable(L, LUA_GLOBALSINDEX);

    lua_pushliteral(L, "AWESOME_RELEASE");
    lua_pushstring(L, AWESOME_RELEASE);
    lua_settable(L, LUA_GLOBALSINDEX);

    /* init hooks */
    globalconf.hooks.manage = LUA_REFNIL;
    globalconf.hooks.unmanage = LUA_REFNIL;
    globalconf.hooks.focus = LUA_REFNIL;
    globalconf.hooks.unfocus = LUA_REFNIL;
    globalconf.hooks.mouse_enter = LUA_REFNIL;
    globalconf.hooks.mouse_leave = LUA_REFNIL;
    globalconf.hooks.arrange = LUA_REFNIL;
    globalconf.hooks.clients = LUA_REFNIL;
    globalconf.hooks.tags = LUA_REFNIL;
    globalconf.hooks.tagged = LUA_REFNIL;
    globalconf.hooks.property = LUA_REFNIL;
    globalconf.hooks.startup_notification = LUA_REFNIL;
    globalconf.hooks.timer = LUA_REFNIL;
#ifdef WITH_DBUS
    globalconf.hooks.dbus = LUA_REFNIL;
#endif

    /* add Lua lib path (/usr/share/awesome/lib by default) */
    luaA_dostring(L, "package.path = package.path .. \";" AWESOME_LUA_LIB_PATH  "/?.lua\"");
    luaA_dostring(L, "package.path = package.path .. \";" AWESOME_LUA_LIB_PATH  "/?/init.lua\"");

    /* add XDG_CONFIG_DIR as include path */
    const char * const *xdgconfigdirs = xdgSearchableConfigDirectories(xdg);
    for(; *xdgconfigdirs; xdgconfigdirs++)
    {
        char *buf;
        a_asprintf(&buf, "package.path = package.path .. \";%s/awesome/?.lua;%s/awesome/?/init.lua\"",
                   *xdgconfigdirs, *xdgconfigdirs);
        luaA_dostring(L, buf);
        p_delete(&buf);
    }
}

static bool
luaA_loadrc(const char *confpath, bool run)
{
    if(!luaL_loadfile(globalconf.L, confpath))
    {
        if(run)
        {
            if(lua_pcall(globalconf.L, 0, LUA_MULTRET, 0))
                fprintf(stderr, "%s\n", lua_tostring(globalconf.L, -1));
            else
            {
                globalconf.conffile = a_strdup(confpath);
                return true;
            }
        }
        else
            lua_pop(globalconf.L, 1);
        return true;
    }
    else
        fprintf(stderr, "%s\n", lua_tostring(globalconf.L, -1));

    return false;
}

/** Load a configuration file.
 * \param xdg An xdg handle to use to get XDG basedir.
 * \param confpatharg The configuration file to load.
 * \param run Run the configuration file.
 */
bool
luaA_parserc(xdgHandle xdg, const char *confpatharg, bool run)
{
    char *confpath = NULL;
    bool ret = false;

    /* try to load, return if it's ok */
    if(confpatharg)
    {
        if(luaA_loadrc(confpatharg, run))
        {
            ret = true;
            goto bailout;
        }
        else if(!run)
            goto bailout;
    }

    confpath = xdgConfigFind("awesome/rc.lua", xdg);

    char *tmp = confpath;

    /* confpath is "string1\0string2\0string3\0\0" */
    do
    {
        if(luaA_loadrc(tmp, run))
        {
            ret = true;
            goto bailout;
        }
        else if(!run)
            goto bailout;
        tmp += a_strlen(tmp) + 1;
    } while(*tmp != 0);

bailout:

    p_delete(&confpath);

    return ret;
}

void
luaA_on_timer(EV_P_ ev_timer *w, int revents)
{
    if(globalconf.hooks.timer != LUA_REFNIL)
        luaA_dofunction(globalconf.L, globalconf.hooks.timer, 0, 0);
    awesome_refresh();
}

/** Push a color as a string onto the stack
 * \param L The Lua VM state.
 * \param c The color to push.
 * \return The number of elements pushed on stack.
 */
int
luaA_pushcolor(lua_State *L, const xcolor_t *c)
{
    uint8_t r = (unsigned)c->red   * 0xff / 0xffff;
    uint8_t g = (unsigned)c->green * 0xff / 0xffff;
    uint8_t b = (unsigned)c->blue  * 0xff / 0xffff;
    uint8_t a = (unsigned)c->alpha * 0xff / 0xffff;
    char s[10];
    int len;
    /* do not print alpha if it's full */
    if(a == 0xff)
        len = snprintf(s, sizeof(s), "#%02x%02x%02x", r, g, b);
    else
        len = snprintf(s, sizeof(s), "#%02x%02x%02x%02x", r, g, b, a);
    lua_pushlstring(L, s, len);
    return 1;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
