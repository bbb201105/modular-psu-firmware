/*
 * EEZ Modular Firmware
 * Copyright (C) 2015-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if OPTION_DISPLAY

#include <eez/util.h>

#include <eez/gui/gui.h>

namespace eez {
namespace gui {

FixPointersFunctionType PROGRESS_fixPointers = nullptr;

EnumFunctionType PROGRESS_enum = nullptr;

DrawFunctionType PROGRESS_draw = [](const WidgetCursor &widgetCursor) {
    const Widget *widget = widgetCursor.widget;

    widgetCursor.currentState->size = sizeof(WidgetState);

    widgetCursor.currentState->data = get(widgetCursor.cursor, widget->data);

    bool refresh = !widgetCursor.previousState || widgetCursor.previousState->data != widgetCursor.currentState->data;

    if (refresh) {
        const Style* style = getStyle(widget->style);

        drawRectangle(widgetCursor.x, widgetCursor.y, (int)widget->w, (int)widget->h, style, false, false, true);

        int percentFrom;
        int percentTo;
        if (widgetCursor.currentState->data.getType() == VALUE_TYPE_RANGE) {
            percentFrom = widgetCursor.currentState->data.getRangeFrom();
            percentTo = widgetCursor.currentState->data.getRangeTo();
        } else {
            percentFrom = 0;
            percentTo = widgetCursor.currentState->data.getInt();
        }

        auto isHorizontal = widget->w > widget->h;
        if (isHorizontal) {
            auto xFrom = percentFrom * widget->w / 100;
            auto xTo = percentTo * widget->w / 100;
            drawRectangle(widgetCursor.x + xFrom, widgetCursor.y, xTo - xFrom, (int)widget->h, style, true, false, true);
        } else {
            auto yFrom = percentFrom * widget->h / 100;
            auto yTo = percentTo * widget->h / 100;
            drawRectangle(widgetCursor.x, widgetCursor.y - yFrom, yTo - yFrom, (int)widget->h, getStyle(widget->style), true, false, true);
        }
    }
};

OnTouchFunctionType PROGRESS_onTouch = nullptr;

} // namespace gui
} // namespace eez

#endif
