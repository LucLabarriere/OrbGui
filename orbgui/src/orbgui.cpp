#include <orb/renderer.hpp>

#include "orb/vk/core.hpp"
#include "orbgui/orbgui.hpp"

namespace orb::gui
{
    struct gui_renderer_t
    {
    };

    instance_t::~instance_t() = default;

    auto instance_t::create(const instance_create_info_t& info) -> orb::result<instance_t>
    {
        auto r = make_box<gui_renderer_t>();

        return instance_t { std::move(r) };
    }

    auto instance_t::draw() -> orb::result<void>
    {
        return {};
    }

    instance_t::instance_t(orb::box<gui_renderer_t> renderer)
        : m_renderer(std::move(renderer))
    {
    }
} // namespace orb::gui
