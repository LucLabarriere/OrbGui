#include "sample.hpp"

#include <orb/eval.hpp>
#include <orb/renderer.hpp>
#include <orb/time.hpp>

#include <thread>

using namespace orb;

static constexpr ui32 max_frames_in_flight = 2;

struct gui_render_pass_t
{
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

struct renderer_t
{
    orb::box<glfw::driver_t>      glfw_driver;
    orb::box<vk::instance_t>      instance;
    orb::box<vk::gpu_t>           gpu;
    orb::box<vk::device_t>        device;
    orb::box<vk::swapchain_t>     swapchain;
    vk::surface_t                 surface;
    orb::box<vk::cmd_pool_t>      graphics_cmd_pool;
    orb::box<vk::cmd_pool_t>      transfer_cmd_pool;
    orb::weak<glfw::window_t>     window;
    orb::weak<vk::queue_family_t> graphics_qf;
    orb::weak<vk::queue_family_t> transfer_qf;

    orb::box<gui_render_pass_t> pass;

    ui32             current_frame = 0;
    ui32             image_index   = 0;
    vk::cmd_buffer_t current_cmd;
};

auto sample_t::create() -> orb::result<sample_t>
{
    auto renderer  = orb::make_box<renderer_t>();
    renderer->pass = orb::make_box<gui_render_pass_t>();

    orb::weak<renderer_t> r = renderer.getmut();
    sample_t              sample { std::move(renderer) };

    try
    {
        r->glfw_driver = glfw::driver_t::create().unwrap();

        r->window = r->glfw_driver->create_window_for_vk().unwrap();
        r->instance =
            vk::instance_builder_t::prepare()
                .unwrap()
                .add_glfw_required_extensions()
                .molten_vk(orb::on_macos ? true : false)
                .add_extension(vk::khr_extensions::device_properties_2)
                .add_extension(vk::extensions::debug_utils)
                .debug_layer(vk::validation_layers::validation)
                .build()
                .unwrap();

        r->surface =
            vk::surface_builder_t::prepare(r->instance->handle, r->window)
                .build()
                .unwrap();

        r->gpu = vk::gpu_selector_t::prepare(r->instance->handle)
                     .unwrap()
                     .prefer_type(vk::gpu_types::discrete)
                     .prefer_type(vk::gpu_types::integrated)
                     .select()
                     .unwrap();

        r->gpu->describe();

        auto [graphics_qf, transfer_qf] = orb::eval | [&] {
            std::span graphics_qfs = r->gpu->queue_family_map->graphics().unwrap();
            std::span transfer_qfs = r->gpu->queue_family_map->transfer().unwrap();

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

        r->graphics_qf = graphics_qf;
        r->transfer_qf = transfer_qf;

        println("- Selected graphics queue family {} with {} queues",
                graphics_qf->index,
                graphics_qf->properties.queueCount);

        println("- Selected transfer queue family {} with {} queues",
                transfer_qf->index,
                transfer_qf->properties.queueCount);

        r->device = vk::device_builder_t::prepare(r->instance->handle)
                        .unwrap()
                        .add_extension(vk::khr_extensions::swapchain)
                        .add_queue(graphics_qf, 1.0f)
                        .add_queue(transfer_qf, 1.0f)
                        .build(*r->gpu)
                        .unwrap();

        r->swapchain =
            vk::swapchain_builder_t::prepare(r->instance.getmut(), r->gpu.getmut(), r->device.getmut(), r->window, &r->surface)
                .unwrap()
                .fb_dimensions_from_window()
                .present_queue_family_index(graphics_qf->index)

                .usage(vk::image_usage_flags::color_attachment)
                .color_space(vk::color_spaces::srgb_nonlinear_khr)
                .format(vk::formats::b8g8r8a8_srgb)
                .format(vk::formats::r8g8b8a8_srgb)
                .format(vk::formats::b8g8r8_srgb)
                .format(vk::formats::r8g8b8_srgb)

                .present_mode(vk::present_modes::mailbox_khr)
                .present_mode(vk::present_modes::immediate_khr)
                .present_mode(vk::present_modes::fifo_khr)

                .build()
                .unwrap();

        r->pass->attachments.add({
            .img_format        = r->swapchain->format.format,
            .samples           = vk::sample_count_flags::_1,
            .load_ops          = vk::attachment_load_ops::clear,
            .store_ops         = vk::attachment_store_ops::store,
            .stencil_load_ops  = vk::attachment_load_ops::dont_care,
            .stencil_store_ops = vk::attachment_store_ops::dont_care,
            .initial_layout    = vk::image_layouts::undefined,
            .final_layout      = vk::image_layouts::present_src_khr,
            .attachment_layout = vk::image_layouts::color_attachment_optimal,
        });

        // const auto [color_descs, color_refs] = r->pass->attachments.spans(0, 1);

        // r->pass->subpasses.add_subpass({
        //     .bind_point = vk::pipeline_bind_points::graphics,
        //     .color_refs = color_refs,
        // });

        // r->pass->subpasses.add_dependency({
        //     .src        = vk::subpass_external,
        //     .dst        = 0,
        //     .src_stage  = vk::pipeline_stage_flags::color_attachment_output,
        //     .dst_stage  = vk::pipeline_stage_flags::color_attachment_output,
        //     .src_access = 0,
        //     .dst_access = vk::access_flags::color_attachment_write,
        // });

        // r->pass->render_pass = vk::render_pass_builder_t::prepare(r->device->handle)
        //                            .unwrap()
        //                            .clear_color({ 0.0f, 0.0f, 0.0f, 1.0f })
        //                            .build(r->pass->subpasses, r->pass->attachments)
        //                            .unwrap();

        // r->pass->views = sample.create_views();
        // r->pass->fbs   = sample.create_fbs();

        const path vs_path { SAMPLE_DIR "main.vs.glsl" };
        const path fs_path { SAMPLE_DIR "main.fs.glsl" };

        vk::spirv_compiler_t compiler;
        compiler
            .option_target_env(shaderc_target_env_vulkan,
                               shaderc_env_version_vulkan_1_2)
            .option_generate_debug_info()
            .option_target_spirv(shaderc_spirv_version_1_3)
            .option_source_language(shaderc_source_language_glsl)
            .option_optimization_level(shaderc_optimization_level_zero)
            .option_warnings_as_errors();

        println("- Reading shader files");
        auto vs_content = vs_path.read_file().unwrap();
        auto fs_content = fs_path.read_file().unwrap();

        println("- Creating shader modules");
        auto vs_shader_module =
            vk::shader_module_builder_t::prepare(r->device.getmut(), &compiler)
                .unwrap()
                .kind(vk::shader_kinds::glsl_vertex)
                .entry_point("main")
                .content(std::move(vs_content))
                .build()
                .unwrap();

        auto fs_shader_module =
            vk::shader_module_builder_t::prepare(r->device.getmut(), &compiler)
                .unwrap()
                .kind(vk::shader_kinds::glsl_fragment)
                .entry_point("main")
                .content(std::move(fs_content))
                .build()
                .unwrap();

        struct Vertex
        {
            std::array<float, 2> pos;
            std::array<float, 3> col;
        };

        println("- Creating graphics pipeline");
        r->pass->pipeline =
            vk::pipeline_builder_t ::prepare(r->device.getmut())
                .unwrap()
                ->shader_stages()
                .stage(vs_shader_module, vk::shader_stage_flags::vertex, "main")
                .stage(fs_shader_module, vk::shader_stage_flags::fragment, "main")
                .dynamic_states()
                .dynamic_state(vk::dynamic_states::viewport)
                .dynamic_state(vk::dynamic_states::scissor)
                .vertex_input()
                .binding<Vertex>(0, vk::vertex_input_rates::vertex)
                .attribute(0, offsetof(Vertex, pos), vk::vertex_formats::vec2_t)
                .attribute(1, offsetof(Vertex, col), vk::vertex_formats::vec3_t)
                .input_assembly()
                .viewport_states()
                .viewport(0.0f, 0.0f, (f32)r->swapchain->width, (f32)r->swapchain->height, 0.0f, 1.0f)
                .scissor(0.0f, 0.0f, r->swapchain->width, r->swapchain->height)
                .rasterizer()
                .multisample()
                .color_blending()
                .new_color_blend_attachment()
                .end_attachment()
                .layout()
                .prepare_pipeline()
                .render_pass(r->pass->render_pass.getmut())
                .subpass(0)
                .build()
                .unwrap();

        println("- Creating synchronization objects");
        // Synchronization
        r->pass->sync_objects = vk::sync_objects_builder_t::prepare(r->device.getmut())
                                    .unwrap()
                                    .semaphores(max_frames_in_flight * 2)
                                    .fences(max_frames_in_flight)
                                    .build()
                                    .unwrap();

        println("- Creating command pool and command buffers");
        r->graphics_cmd_pool =
            vk::cmd_pool_builder_t::prepare(r->device.getmut(), graphics_qf->index)
                .unwrap()
                .flag(vk::command_pool_create_flags::reset_command_buffer_bit)
                .build()
                .unwrap();

        r->transfer_cmd_pool =
            vk::cmd_pool_builder_t::prepare(r->device.getmut(), transfer_qf->index)
                .unwrap()
                .flag(vk::command_pool_create_flags::reset_command_buffer_bit)
                .build()
                .unwrap();

        println("- Creating command buffers");
        r->pass->draw_cmds =
            r->graphics_cmd_pool->alloc_cmds(max_frames_in_flight).unwrap();

        std::vector<Vertex> vertices = {
            { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
            {  { 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
            {   { 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
            {  { -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } }
        };

        std::vector<ui16> indices = { 0, 1, 2, 2, 3, 0 };

        println("- Creating vertex buffer");
        r->pass->vertex_buffer =
            vk::vertex_buffer_builder_t::prepare(r->device.getmut())
                .unwrap()
                .vertices<Vertex>(vertices)
                .buffer_usage_flag(vk::buffer_usage_flags::transfer_destination)
                .memory_flags(vk::vma_alloc_flags::dedicated_memory)
                .build()
                .unwrap();

        println("- Creating index buffer");
        r->pass->index_buffer =
            vk::index_buffer_builder_t::prepare(r->device.getmut())
                .unwrap()
                .indices(std::span<const ui16> { indices })
                .buffer_usage_flag(vk::buffer_usage_flags::transfer_destination)
                .memory_flags(vk::vma_alloc_flags::dedicated_memory)
                .build()
                .unwrap();

        println("- Creating staging buffer");
        auto staging_buffer = vk::staging_buffer_builder_t::prepare(
                                  r->device.getmut(),
                                  r->pass->vertex_buffer.size)
                                  .unwrap()
                                  .build()
                                  .unwrap();

        println("- Copying vertices to staging buffer");
        staging_buffer.transfer(vertices.data(), sizeof(Vertex) * vertices.size())
            .unwrap();

        println("- Copying staging buffer to vertex buffer");
        auto cpy_cmd = r->transfer_cmd_pool->alloc_cmds(1).unwrap().get(0).unwrap();

        cpy_cmd.begin_one_time().unwrap();
        cpy_cmd.copy_buffer(staging_buffer.buffer, r->pass->vertex_buffer.buffer, r->pass->vertex_buffer.size);
        cpy_cmd.end().unwrap();

        println("- Submitting copy command buffer");
        vk::submit_helper_t::prepare()
            .cmd_buffer(&cpy_cmd.handle)
            .wait_stage(vk::pipeline_stage_flags::transfer)
            .submit(transfer_qf->queues.front())
            .unwrap();

        r->device->wait().unwrap();

        println("- Copying indices to staging buffer");
        staging_buffer.transfer(indices.data(), sizeof(ui16) * indices.size())
            .unwrap();

        println("- Copying staging buffer to index buffer");
        cpy_cmd = r->transfer_cmd_pool->alloc_cmds(1).unwrap().get(0).unwrap();

        cpy_cmd.begin_one_time().unwrap();
        println("Index buffer size: {}", r->pass->index_buffer.size);
        cpy_cmd.copy_buffer(staging_buffer.buffer, r->pass->index_buffer.buffer, r->pass->index_buffer.size);
        cpy_cmd.end().unwrap();

        println("- Submitting copy command buffer");
        vk::submit_helper_t::prepare()
            .cmd_buffer(&cpy_cmd.handle)
            .wait_stage(vk::pipeline_stage_flags::transfer)
            .submit(transfer_qf->queues.front())
            .unwrap();

        r->device->wait().unwrap();

        ui32 frame = 0;

        r->device->wait().unwrap();
    }
    catch (const orb::exception& e)
    {
        return orb::error_t { e.what() };
    }

    return std::move(sample);
}

auto sample_t::window_should_close() const -> bool
{
    return m_renderer->window->should_close();
}

auto sample_t::begin_loop_step() -> orb::result<void>
{
    auto r = m_renderer.getmut();
    r->glfw_driver->poll_events();
    ui32 frame = r->current_frame;

    if (r->window->minimized())
    {
        using namespace std::literals;
        std::this_thread::sleep_for(orb::milliseconds_t(100));
        return {};
    }

    auto fences               = r->pass->sync_objects.fences(frame, 1);
    auto img_avail_sems       = r->pass->sync_objects.semaphores(frame, 1);
    auto render_finished_sems = r->pass->sync_objects.semaphores(frame + max_frames_in_flight, 1);

    // Wait fences
    fences.wait().unwrap();

    // Acquire the next swapchain image
    auto res = vk::acquire_img(*r->swapchain, img_avail_sems.handles.back(), nullptr);

    if (res.require_sc_rebuild())
    {
        r->device->wait().unwrap();
        r->swapchain->rebuild().unwrap();
        r->pass->views = create_views();
        r->pass->fbs   = create_fbs();

        return {};
    }
    else if (res.is_error())
    {
        return error_t { "Acquire img error: {}", vk::vkres::get_repr(res.error()) };
    }

    // Reset fences
    fences.reset().unwrap();

    r->image_index = res.img_index();

    // Render to the framebuffer
    r->pass->render_pass->begin_info.framebuffer       = r->pass->fbs.handles[r->image_index];
    r->pass->render_pass->begin_info.renderArea.extent = r->swapchain->extent;

    // Begin command buffer recording
    r->current_cmd = r->pass->draw_cmds.get(frame).unwrap();
    r->current_cmd.begin_one_time().unwrap();

    // Begin the render pass
    r->pass->render_pass->begin(r->current_cmd.handle);

    // Bind the graphics pipeline
    vkCmdBindPipeline(r->current_cmd.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pass->pipeline->handle);
    std::array<VkDeviceSize, 1> offsets = { 0 };
    vkCmdBindVertexBuffers(r->current_cmd.handle, 0, 1, &r->pass->vertex_buffer.buffer, offsets.data());
    vkCmdBindIndexBuffer(r->current_cmd.handle, r->pass->index_buffer.buffer, 0, r->pass->index_buffer.index_type);

    // Set viewport and scissor
    auto& viewport        = r->pass->pipeline->viewports.back();
    auto& scissor         = r->pass->pipeline->scissors.back();
    viewport.width        = static_cast<f32>(r->swapchain->width);
    viewport.height       = static_cast<f32>(r->swapchain->height);
    scissor.extent.width  = r->swapchain->width;
    scissor.extent.height = r->swapchain->height;
    vkCmdSetViewport(r->current_cmd.handle, 0, 1, &viewport);
    vkCmdSetScissor(r->current_cmd.handle, 0, 1, &scissor);

    // Draw quad
    vkCmdDrawIndexed(r->current_cmd.handle, static_cast<ui32>(r->pass->index_buffer.count), 1, 0, 0, 0);

    return {};
}

auto sample_t::end_loop_step() -> orb::result<void>
{
    auto r = m_renderer.getmut();

    // End the render pass
    r->pass->render_pass->end(r->current_cmd.handle);

    // End command buffer recording
    r->current_cmd.end().unwrap();

    auto frame                = r->current_frame;
    auto img_index            = r->image_index;
    auto fences               = r->pass->sync_objects.fences(frame, 1);
    auto img_avail_sems       = r->pass->sync_objects.semaphores(frame, 1);
    auto render_finished_sems = r->pass->sync_objects.semaphores(frame + max_frames_in_flight, 1);

    // Submit render
    vk::submit_helper_t::prepare()
        .wait_semaphores(img_avail_sems.handles)
        .signal_semaphores(render_finished_sems.handles)
        .cmd_buffer(&r->current_cmd.handle)
        .wait_stage(vk::pipeline_stage_flags::color_attachment_output)
        .submit(r->graphics_qf->queues.front(), fences.handles.back())
        .unwrap();

    // Present the rendered image
    auto present_res = vk::present_helper_t::prepare()
                           .swapchain(*r->swapchain)
                           .wait_semaphores(render_finished_sems.handles)
                           .img_index(img_index)
                           .present(r->graphics_qf->queues.front());

    r->current_frame = (frame + 1) % max_frames_in_flight;

    if (present_res.require_sc_rebuild())
    {
        return {};
    }
    else if (present_res.is_error())
    {
        return error_t { "Frame present error: {}", vk::vkres::get_repr(present_res.error()) };
    }

    return {};
}

auto sample_t::terminate() -> orb::result<void>
{
    return m_renderer->device->wait();
}

auto sample_t::create_views() -> vk::views_t
{
    return vk::views_builder_t::prepare(m_renderer->device->handle)
        .unwrap()
        .images(m_renderer->swapchain->images)
        .aspect_mask(vk::image_aspect_flags::color)
        .format(vk::formats::b8g8r8a8_srgb)
        .build()
        .unwrap();
}

auto sample_t::create_fbs() -> vk::framebuffers_t
{
    return vk::framebuffers_builder_t::prepare(m_renderer->device.getmut(),
                                               m_renderer->pass->render_pass->handle)
        .unwrap()
        .size(m_renderer->swapchain->width, m_renderer->swapchain->height)
        .attachments(m_renderer->pass->views.handles)
        .build()
        .unwrap();
}

sample_t::sample_t(orb::box<renderer_t> renderer)
    : m_renderer(std::move(renderer))
{
}

sample_t::~sample_t() = default;
