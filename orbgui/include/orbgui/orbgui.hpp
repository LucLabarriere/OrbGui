#pragma once

#include <orb/box.hpp>
#include <orb/result.hpp>
#include <orb/vk/enums.hpp>

#define ORB_DEFINE_VK_HANDLE(object) typedef struct object##_T*(object);

ORB_DEFINE_VK_HANDLE(VkDevice)

namespace orb::vk
{
    struct views_t;
    struct framebuffers_t;
    struct device_t;
} // namespace orb::vk

namespace orb::gui
{
    struct renderer_t;

    struct instance_create_info_t
    {
        orb::weak<vk::device_t> device;
        vk::format format;
    };

    class instance_t
    {
    public:
        ~instance_t();

        instance_t(instance_t const&)                        = delete;
        instance_t(instance_t&&)                             = default;
        auto operator=(instance_t const&) -> instance_t&     = delete;
        auto operator=(instance_t&&) noexcept -> instance_t& = default;

        static auto create(const instance_create_info_t& device) -> orb::result<instance_t>;

        auto draw() -> orb::result<void>;

    private:
        orb::box<renderer_t> m_renderer;

        explicit instance_t(orb::box<renderer_t> renderer);

        auto create_views() -> orb::vk::views_t;
        auto create_fbs() -> orb::vk::framebuffers_t;
    };
} // namespace orb::gui
