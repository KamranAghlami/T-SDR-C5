// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/menu.hpp"

#include <algorithm>
#include <cstdio>

#include "core/gain.hpp"

namespace core
{
    namespace
    {
        constexpr unsigned manual_gain_step = 8;

        template <typename T, size_t N> T next_in(const std::array<T, N> &values, T current)
        {
            const auto it = std::ranges::find(values, current);

            return (it == values.end() || it + 1 == values.end()) ? values.front() : *(it + 1);
        }
    }

    const char *menu_label(menu_item item)
    {
        switch (item)
        {
        case menu_item::view:
            return "View";
        case menu_item::span:
            return "Span";
        case menu_item::step:
            return "Step";
        case menu_item::detector:
            return "Detector";
        case menu_item::gain:
            return "Gain";
        case menu_item::colormap:
            return "Colours";
        case menu_item::brightness:
            return "Bright";
        case menu_item::about:
            return "About";
        }

        return "";
    }

    bool menu_change(state &s, menu_item item, unsigned max_gain)
    {
        switch (item)
        {
        case menu_item::view:
            s.view = s.view == view_id::channel ? view_id::band : view_id::channel;
            return true;

        case menu_item::span:
            // The band view always shows the whole band.
            if (s.view == view_id::band)
                return false;

            s.span_mhz = next_in(spans_mhz, s.span_mhz);
            return true;

        case menu_item::step:
            s.step_mhz = next_in(steps_mhz, s.step_mhz);
            return true;

        case menu_item::detector:
            s.detector = s.detector == detector::peak ? detector::mean : detector::peak;
            return true;

        case menu_item::gain:
            // AUTO -> AGC -> 8, 16, ... up to max -> AUTO
            if (s.gain == gain_auto)
                s.gain = gain_agc;
            else if (s.gain == gain_agc)
                s.gain = static_cast<uint8_t>(std::min(manual_gain_step, max_gain));
            else if (s.gain + manual_gain_step <= max_gain)
                s.gain = static_cast<uint8_t>(s.gain / manual_gain_step * manual_gain_step + manual_gain_step);
            else
                s.gain = gain_auto;
            return true;

        case menu_item::colormap:
            s.colormap = s.colormap == colormap_id::turbo ? colormap_id::grayscale : colormap_id::turbo;
            return true;

        case menu_item::brightness:
            s.brightness = next_in(brightness_levels, s.brightness);
            return true;

        case menu_item::about:
            return false;
        }

        return false;
    }

    void menu_value(const state &s, menu_item item, char *out, size_t size, const char *version)
    {
        switch (item)
        {
        case menu_item::view:
            std::snprintf(out, size, "%s", s.view == view_id::channel ? "Channel" : "Band");
            break;

        case menu_item::span:
            if (s.view == view_id::band)
                std::snprintf(out, size, "Whole band");
            else
                std::snprintf(out, size, "%u MHz", s.span_mhz);
            break;

        case menu_item::step:
            std::snprintf(out, size, "%u MHz", s.step_mhz);
            break;

        case menu_item::detector:
            std::snprintf(out, size, "%s", s.detector == detector::peak ? "Peak" : "Mean");
            break;

        case menu_item::gain:
            if (s.gain == gain_auto)
                std::snprintf(out, size, "Auto");
            else if (s.gain == gain_agc)
                std::snprintf(out, size, "HW AGC");
            else
                std::snprintf(out, size, "%u", s.gain);
            break;

        case menu_item::colormap:
            std::snprintf(out, size, "%s", s.colormap == colormap_id::turbo ? "Turbo" : "Grey");
            break;

        case menu_item::brightness:
            std::snprintf(out, size, "%u%%", s.brightness);
            break;

        case menu_item::about:
            std::snprintf(out, size, "%s", version ? version : "");
            break;
        }
    }
}
