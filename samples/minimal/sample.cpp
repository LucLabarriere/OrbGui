#include "sample.hpp"

#include <orb/eval.hpp>
#include <orb/renderer.hpp>
#include <orb/time.hpp>

#include <thread>

using namespace orb;

static constexpr ui32 max_frames_in_flight = 2;

struct renderer_t
{
    box<glfw::driver_t>      glfw_driver;
    weak<glfw::window_t>     window;
    box<vk::instance_t>      instance;
    vk::surface_t            surface;
    box<vk::gpu_t>           gpu;
    weak<vk::queue_family_t> graphics_qf;
    weak<vk::queue_family_t> transfer_qf;
    box<vk::device_t>        device;
    box<vk::swapchain_t>     swapchain;
    vk::attachments_t        attachments;
    vk::subpasses_t          subpasses;
    vk::render_pass_t        render_pass;
    vk::views_t              views;
    vk::fences_t             frame_ready_fences;
    vk::semaphores_t         blit_finished_semaphores;
    vk::semaphores_t         image_avail_semaphores;
    box<vk::cmd_pool_t>      graphics_cmd_pool;
    box<vk::cmd_pool_t>      transfer_cmd_pool;
    vk::cmd_buffers_t        blit_cmds;
    ui32                     frame     = 0;
    ui32                     img_index = 0;
};

sample_t::~sample_t() = default;

auto sample_t::create() -> orb::result<sample_t>
{
    auto b = make_box<renderer_t>();

    b->glfw_driver = glfw::driver_t::create().unwrap();

    b->window   = b->glfw_driver->create_window_for_vk().unwrap();
    b->instance = vk::instance_builder_t::prepare()
                      .unwrap()
                      .add_glfw_required_extensions()
                      .molten_vk(orb::on_macos ? true : false)
                      .add_extension(vk::khr_extensions::device_properties_2)
                      .add_extension(vk::extensions::debug_utils)
                      .debug_layer(vk::validation_layers::validation)
                      .build()
                      .unwrap();

    b->surface = vk::surface_builder_t::prepare(b->instance->handle, b->window).build().unwrap();

    b->gpu = vk::gpu_selector_t::prepare(b->instance->handle)
                 .unwrap()
                 .prefer_type(vk::gpu_type::discrete)
                 .prefer_type(vk::gpu_type::integrated)
                 .select()
                 .unwrap();

    b->gpu->describe();

    auto [graphics_qf, transfer_qf] = orb::eval | [&] {
        std::span graphics_qfs = b->gpu->queue_family_map->graphics().unwrap();
        std::span transfer_qfs = b->gpu->queue_family_map->transfer().unwrap();

        auto graphics_qf = graphics_qfs.front();

        auto transfer_qf = orb::eval | [&] {
            for (auto qf : transfer_qfs)
            {
                if (qf->index != graphics_qf->index)
                {
                    return qf;
                }
            }

            return transfer_qfs.front();
        };

        return std::make_tuple(graphics_qf, transfer_qf);
    };

    b->graphics_qf = graphics_qf;
    b->transfer_qf = transfer_qf;

    fmt::println("- Selected graphics queue family {} with {} queues",
                 graphics_qf->index,
                 graphics_qf->properties.queueCount);

    fmt::println("- Selected transfer queue family {} with {} queues",
                 transfer_qf->index,
                 transfer_qf->properties.queueCount);

    b->device = vk::device_builder_t::prepare(b->instance->handle)
                    .unwrap()
                    .add_extension(vk::khr_extensions::swapchain)
                    .add_queue(graphics_qf, 1.0f)
                    .add_queue(transfer_qf, 1.0f)
                    .build(*b->gpu)
                    .unwrap();

    b->swapchain = vk::swapchain_builder_t::prepare(b->instance.getmut(),
                                                    b->gpu.getmut(),
                                                    b->device.getmut(),
                                                    b->window,
                                                    &b->surface)
                       .unwrap()
                       .fb_dimensions_from_window()
                       .present_queue_family_index(graphics_qf->index)

                       .usage(vk::image_usage_flag::color_attachment)
                       .usage(vk::image_usage_flag::transfer_dst)
                       .color_space(vk::color_space::srgb_nonlinear_khr)
                       .format(vk::format::b8g8r8a8_srgb)
                       .format(vk::format::r8g8b8a8_srgb)
                       .format(vk::format::b8g8r8_srgb)
                       .format(vk::format::r8g8b8_srgb)

                       .present_mode(vk::present_mode::mailbox_khr)
                       .present_mode(vk::present_mode::immediate_khr)
                       .present_mode(vk::present_mode::fifo_khr)

                       .build()
                       .unwrap();

    fmt::println("- Creating synchronization objects");
    // Synchronization
    b->frame_ready_fences = vk::fences_builder_t::create(b->device.getmut(), max_frames_in_flight).unwrap();

    b->blit_finished_semaphores = vk::semaphores_builder_t::prepare(b->device.getmut())
                                      .unwrap()
                                      .count(b->swapchain->images.size())
                                      .stage(vk::pipeline_stage_flag::color_attachment_output)
                                      .build()
                                      .unwrap();

    b->image_avail_semaphores = vk::semaphores_builder_t::prepare(b->device.getmut())
                                    .unwrap()
                                    .count(max_frames_in_flight)
                                    .stage(vk::pipeline_stage_flag::transfer)
                                    .build()
                                    .unwrap();

    fmt::println("- Creating command pool and command buffers");
    b->graphics_cmd_pool = vk::cmd_pool_builder_t::prepare(b->device.getmut(), b->graphics_qf->index)
                               .unwrap()
                               .flag(vk::command_pool_create_flag::reset_command_buffer)
                               .build()
                               .unwrap();

    b->transfer_cmd_pool = vk::cmd_pool_builder_t::prepare(b->device.getmut(), b->transfer_qf->index)
                               .unwrap()
                               .flag(vk::command_pool_create_flag::reset_command_buffer)
                               .build()
                               .unwrap();

    fmt::println("- Creating blit command buffers");
    b->blit_cmds = b->transfer_cmd_pool->alloc_cmds(max_frames_in_flight).unwrap();

    return sample_t { std::move(b) };
}

auto sample_t::window_should_close() const -> bool
{
    return m_renderer->window->should_close();
}

auto sample_t::begin_loop_step() -> orb::result<void>
{
    m_resize_required = false;

    m_renderer->glfw_driver->poll_events();

    if (m_renderer->window->minimized())
    {
        using namespace std::literals;
        std::this_thread::sleep_for(orb::milliseconds_t(100));
        return {};
    }

    auto frame_fence = m_renderer->frame_ready_fences[m_renderer->frame];
    auto img_avail   = m_renderer->image_avail_semaphores.view(m_renderer->frame, 1);

    // Wait fences
    frame_fence.wait().unwrap();

    // Acquire the next swapchain image
    auto res = vk::acquire_img(*m_renderer->swapchain, img_avail.handles.back(), nullptr);

    if (res.require_sc_rebuild())
    {
        m_renderer->device->wait().unwrap();
        m_renderer->swapchain->rebuild().unwrap();
        m_resize_required = true;
        return {};
    }
    else if (res.is_error())
    {
        return orb::error_t { "Acquire img error: {}", vk::vkres::get_repr(res.error()) };
    }

    // Reset fences
    frame_fence.reset().unwrap();

    m_renderer->img_index = res.img_index();

    return {};
}

auto sample_t::end_loop_step(VkImage gui_img, orb::vk::semaphores_view_t& gui_rendered_sem) -> orb::result<void>
{
    auto blit_cmd      = m_renderer->blit_cmds.get(m_renderer->frame).unwrap();
    auto frame_fence   = m_renderer->frame_ready_fences[m_renderer->frame];
    auto img_avail     = m_renderer->image_avail_semaphores.view(m_renderer->frame, 1);
    auto blit_finished = m_renderer->blit_finished_semaphores.view(m_renderer->img_index, 1);

    blit_cmd.begin_one_time().unwrap();

    auto swapchain_img = m_renderer->swapchain->images[m_renderer->img_index];

    vk::transition_layout(blit_cmd.handle,
                          gui_img,
                          vk::image_layout::undefined,
                          vk::image_layout::transfer_src_optimal);

    vk::transition_layout(blit_cmd.handle,
                          swapchain_img,
                          vk::image_layout::undefined,
                          vk::image_layout::transfer_dst_optimal);

    vk::copy_img(blit_cmd.handle, gui_img, swapchain_img, m_renderer->swapchain->extent);

    vk::transition_layout(blit_cmd.handle,
                          swapchain_img,
                          vk::image_layout::transfer_dst_optimal,
                          vk::image_layout::present_src_khr);
    blit_cmd.end();

    auto wait_semaphores = img_avail.concat(gui_rendered_sem);

    // Submit blit
    vk::submit_helper_t::prepare()
        .wait_semaphores(wait_semaphores)
        .signal_semaphores(blit_finished.handles)
        .cmd_buffer(&blit_cmd.handle)
        .submit(m_renderer->transfer_qf->queues.front(), frame_fence.handle)
        .unwrap();

    // Present the rendered image
    auto present_res = vk::present_helper_t::prepare()
                           .swapchain(*m_renderer->swapchain)
                           .wait_semaphores(blit_finished.handles)
                           .img_index(m_renderer->img_index)
                           .present(m_renderer->graphics_qf->queues.front());

    if (present_res.require_sc_rebuild())
    {
        m_resize_required = true;
        return {};
    }
    else if (present_res.is_error())
    {
        return orb::error_t { "Frame present error: {}", vk::vkres::get_repr(present_res.error()) };
    }

    m_renderer->frame = (m_renderer->frame + 1) % max_frames_in_flight;

    return {};
}

auto sample_t::terminate() -> orb::result<void>
{
    return m_renderer->device->wait();
}

auto sample_t::get_gui_create_info() -> orb::gui::instance_create_info_t
{
    return orb::gui::instance_create_info_t {
        .device         = m_renderer->device.getmut(),
        .extent_width   = m_renderer->swapchain->extent.width,
        .extent_height  = m_renderer->swapchain->extent.height,
        .graphics_queue = m_renderer->graphics_qf->queues.front(),
        .transfer_queue = m_renderer->transfer_qf->queues.front(),
        .graphics_qf    = m_renderer->graphics_qf->index,
        .transfer_qf    = m_renderer->transfer_qf->index,
    };
}

sample_t::sample_t(orb::box<renderer_t> renderer)
    : m_renderer(std::move(renderer))
{
}
