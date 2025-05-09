#include <orb/renderer.hpp>

#include "orbgui/orbgui.hpp"
#include "orb/vk/core.hpp"

namespace orb::gui
{
    struct renderer_t
    {
        orb::weak<vk::device_t>           device;
        orb::box<vk::render_pass_t>       render_pass;
        orb::box<vk::graphics_pipeline_t> pipeline;

        vk::attachments_t   attachments;
        vk::subpasses_t     subpasses;
        vk::vertex_buffer_t vertex_buffer;
        vk::index_buffer_t  index_buffer;
        vk::sync_objects_t  sync_objects;
        vk::views_t         views;
        vk::framebuffers_t  fbs;
        vk::cmd_buffers_t   draw_cmds;
    };

    instance_t::~instance_t()
    {
        [[maybe_unused]] auto r = m_renderer.getmut();
    }

    auto instance_t::create(const instance_create_info_t& info) -> orb::result<instance_t>
    {
        auto renderer = orb::make_box<renderer_t>();
        //renderer->device = info.device;

        //renderer->attachments.add({
        //    .img_format        = r->swapchain->format.format,
        //    .samples           = vk::sample_count_flag::_1,
        //    .load_ops          = vk::attachment_load_op::clear,
        //    .store_ops         = vk::attachment_store_op::store,
        //    .stencil_load_ops  = vk::attachment_load_op::dont_care,
        //    .stencil_store_ops = vk::attachment_store_op::dont_care,
        //    .initial_layout    = vk::image_layout::undefined,
        //    .final_layout      = vk::image_layout::present_src_khr,
        //    .attachment_layout = vk::image_layout::color_attachment_optimal,
        //});

        //const auto [color_descs, color_refs] = renderer->attachments.spans(0, 1);

        //renderer->subpasses.add_subpass({
        //    .bind_point = vk::pipeline_bind_point::graphics,
        //    .color_refs = color_refs,
        //});

        //renderer->subpasses.add_dependency({
        //    .src        = vk::subpass_external,
        //    .dst        = 0,
        //    .src_stage  = vk::pipeline_stage_flag::color_attachment_output,
        //    .dst_stage  = vk::pipeline_stage_flag::color_attachment_output,
        //    .src_access = 0,
        //    .dst_access = vk::access_flag::color_attachment_write,
        //});

        //renderer->render_pass = vk::render_pass_builder_t::prepare(info.device->handle)
        //                            .unwrap()
        //                            .clear_color({ 0.0f, 0.0f, 0.0f, 1.0f })
        //                            .build(renderer->subpasses, renderer->attachments)
        //                            .unwrap();

        //renderer->views = sample.create_views();
        //renderer->fbs   = sample.create_fbs();

        return instance_t { std::move(renderer) };
    }

    auto instance_t::draw() -> orb::result<void>
    {
        return {};
    }

    instance_t::instance_t(orb::box<renderer_t> renderer)
        : m_renderer(std::move(renderer))
    {
    }

    //auto instance_t::create_views() -> vk::views_t
    //{
    //    return vk::views_builder_t::prepare(m_renderer->device->handle)
    //        .unwrap()
    //        .images(m_renderer->swapchain->images)
    //        .aspect_mask(vk::image_aspect_flags::color)
    //        .format(vk::formats::b8g8r8a8_srgb)
    //        .build()
    //        .unwrap();
    //}

    //auto instance_t::create_fbs() -> vk::framebuffers_t
    //{
    //    return vk::framebuffers_builder_t::prepare(m_renderer->device.getmut(),
    //                                               m_renderer->pass->render_pass->handle)
    //        .unwrap()
    //        .size(m_renderer->swapchain->width, m_renderer->swapchain->height)
    //        .attachments(m_renderer->pass->views.handles)
    //        .build()
    //        .unwrap();
    //}
} // namespace orb::gui
