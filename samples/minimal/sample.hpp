#pragma once

#include <orb/box.hpp>
#include <orb/result.hpp>

namespace orb::vk
{
    struct views_t;
    struct framebuffers_t;
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
    [[nodiscard]] auto end_loop_step() -> orb::result<void>;

    auto terminate() -> orb::result<void>;

private:
    orb::box<renderer_t> m_renderer;

    explicit sample_t(orb::box<renderer_t> renderer);

    auto create_views() -> orb::vk::views_t;
    auto create_fbs() -> orb::vk::framebuffers_t;
};
