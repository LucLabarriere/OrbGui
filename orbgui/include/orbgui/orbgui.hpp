#pragma once

#include <orb/box.hpp>
#include <orb/result.hpp>
#include <orb/vk/enums.hpp>

#define ORB_DEFINE_VK_HANDLE(object) typedef struct object##_T*(object);

ORB_DEFINE_VK_HANDLE(VkQueue)
ORB_DEFINE_VK_HANDLE(VkImage)

namespace orb::vk
{
    class device_t;
    class semaphores_view_t;
}

namespace orb::gui
{
    struct gui_renderer_t;

    struct instance_create_info_t
    {
        weak<vk::device_t> device;
        ui32               extent_width;
        ui32               extent_height;
        VkQueue            graphics_queue;
        VkQueue            transfer_queue;
        ui32               graphics_qf;
        ui32               transfer_qf;
    };

    class instance_t
    {
    public:
        ~instance_t();

        instance_t(instance_t const&)                        = delete;
        instance_t(instance_t&&)                             = default;
        auto operator=(instance_t const&) -> instance_t&     = delete;
        auto operator=(instance_t&&) noexcept -> instance_t& = default;

        static auto create(instance_create_info_t&& info) -> orb::result<instance_t>;

        auto render() -> orb::result<void>;
        auto on_resize() -> orb::result<void>;

        [[nodiscard]] auto rendered_image() const -> VkImage;
        [[nodiscard]] auto render_finished() -> vk::semaphores_view_t&;

    private:
        box<gui_renderer_t> m_renderer;

        explicit instance_t(orb::box<gui_renderer_t> renderer);
    };
} // namespace orb::gui
