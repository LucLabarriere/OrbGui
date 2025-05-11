#pragma once

#include "orbgui/orbgui.hpp"

#include <orb/box.hpp>
#include <orb/result.hpp>

#define ORB_DEFINE_VK_HANDLE(object) typedef struct object##_T*(object);

ORB_DEFINE_VK_HANDLE(VkImage)

namespace orb::vk
{
    struct views_t;
    struct framebuffers_t;
    struct semaphores_view_t;
} // namespace orb::vk

struct renderer_t;

class sample_t
{
public:
    ~sample_t();

    sample_t(sample_t const&)                        = delete;
    sample_t(sample_t&&) noexcept                    = default;
    auto operator=(sample_t const&) -> sample_t&     = delete;
    auto operator=(sample_t&&) noexcept -> sample_t& = default;

    static auto create() -> orb::result<sample_t>;

    [[nodiscard]] auto window_should_close() const -> bool;
    [[nodiscard]] auto begin_loop_step() -> orb::result<void>;
    [[nodiscard]] auto end_loop_step(VkImage                     gui_img,
                                     orb::vk::semaphores_view_t& gui_rendered_sem) -> orb::result<void>;

    auto terminate() -> orb::result<void>;

    auto get_gui_create_info() -> orb::gui::instance_create_info_t;

    [[nodiscard]] auto is_resize_required() const -> bool { return m_resize_required; }

private:
    orb::box<renderer_t> m_renderer;
    bool                 m_resize_required = false;

    explicit sample_t(orb::box<renderer_t> renderer);
};
