/*
 * widget.h - widget managing header
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
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

#ifndef AWESOME_WIDGET_H
#define AWESOME_WIDGET_H

#include "mouse.h"

LUA_OBJECT_FUNCS(widget_t, widget, "widget");

struct widget_node_t
{
    /** The widget object */
    widget_t *widget;
    /** The geometry where the widget was drawn */
    area_t geometry;
};

widget_t *widget_getbycoords(position_t, widget_node_array_t *, int, int, int16_t *, int16_t *);
void widget_render(wibox_t *);

void luaA_table2widgets(lua_State *, widget_node_array_t *);

void widget_invalidate_bywidget(widget_t *);
void widget_invalidate_bytype(int, widget_constructor_t *);

widget_constructor_t widget_textbox;
widget_constructor_t widget_progressbar;
widget_constructor_t widget_graph;
widget_constructor_t widget_systray;
widget_constructor_t widget_imagebox;

void widget_node_delete(widget_node_t *);
ARRAY_FUNCS(widget_node_t, widget_node, widget_node_delete)

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
