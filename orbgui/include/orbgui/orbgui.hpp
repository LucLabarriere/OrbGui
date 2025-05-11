#pragma once

#include <orb/box.hpp>
#include <orb/result.hpp>
#include <orb/vk/enums.hpp>

#define ORB_DEFINE_VK_HANDLE(object) typedef struct object##_T*(object);

ORB_DEFINE_VK_HANDLE(VkDevice)

namespace orb::gui
{
    struct gui_renderer_t;

    struct instance_create_info_t
    {
    };

    class instance_t
    {
    public:
        ~instance_t();

        instance_t(instance_t const&)                        = delete;
        instance_t(instance_t&&)                             = default;
        auto operator=(instance_t const&) -> instance_t&     = delete;
        auto operator=(instance_t&&) noexcept -> instance_t& = default;

        static auto create(const instance_create_info_t& info) -> orb::result<instance_t>;

        auto draw() -> orb::result<void>;

    private:
        box<gui_renderer_t> m_renderer;

        explicit instance_t(orb::box<gui_renderer_t> renderer);
    };
} // namespace orb::gui
