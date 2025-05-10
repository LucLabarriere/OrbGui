#include <span>
#include <thread>

#include <orb/eval.hpp>
#include <orb/files.hpp>
#include <orb/flux.hpp>
#include <orb/renderer.hpp>
#include <orb/time.hpp>

using namespace orb;

static constexpr ui32 max_frames_in_flight = 2;

namespace
{
    struct backend_t
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
        vk::framebuffers_t       fbs;
    };

    auto initialize_backend()
    {
        auto b = make_box<backend_t>();

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

        return b;
    }

    struct gui_backend_t
    {
        vk::attachments_t            attachments;
        vk::subpasses_t              subpasses;
        box<vk::render_pass_t>       render_pass;
        vk::views_t                  views;
        vk::framebuffers_t           fbs;
        vk::shader_module_t          vs_shader_module;
        vk::shader_module_t          fs_shader_module;
        box<vk::graphics_pipeline_t> pipeline;
        vk::sync_objects_t           sync_objects;
        box<vk::cmd_pool_t>          graphics_cmd_pool;
        box<vk::cmd_pool_t>          transfer_cmd_pool;
        vk::cmd_buffers_t            draw_cmds;
        vk::vertex_buffer_t          vertex_buffer;
        vk::index_buffer_t           index_buffer;

        void create_views(backend_t& backend)
        {
            this->views = vk::views_builder_t::prepare(backend.device->handle)
                              .unwrap()
                              .images(backend.swapchain->images)
                              .aspect_mask(vk::image_aspect_flag::color)
                              .format(vk::format::b8g8r8a8_srgb)
                              .build()
                              .unwrap();
        };

        void create_fbs(backend_t& backend)
        {
            this->fbs = vk::framebuffers_builder_t::prepare(backend.device.getmut(), render_pass->handle)
                            .unwrap()
                            .size(backend.swapchain->width, backend.swapchain->height)
                            .attachments(views.handles)
                            .build()
                            .unwrap();
        };
    };

    auto initialize_gui_backend(auto& backend)
    {
        auto gb = make_box<gui_backend_t>();

        gb->attachments.add({
            .img_format        = backend->swapchain->format.format,
            .samples           = vk::sample_count_flag::_1,
            .load_ops          = vk::attachment_load_op::clear,
            .store_ops         = vk::attachment_store_op::store,
            .stencil_load_ops  = vk::attachment_load_op::dont_care,
            .stencil_store_ops = vk::attachment_store_op::dont_care,
            .initial_layout    = vk::image_layout::undefined,
            .final_layout      = vk::image_layout::present_src_khr,
            .attachment_layout = vk::image_layout::color_attachment_optimal,
        });

        const auto [color_descs, color_refs] = gb->attachments.spans(0, 1);

        gb->subpasses.add_subpass({
            .bind_point = vk::pipeline_bind_point::graphics,
            .color_refs = color_refs,
        });

        gb->subpasses.add_dependency({
            .src        = vk::subpass_external,
            .dst        = 0,
            .src_stage  = vk::pipeline_stage_flag::color_attachment_output,
            .dst_stage  = vk::pipeline_stage_flag::color_attachment_output,
            .src_access = 0,
            .dst_access = vk::access_flag::color_attachment_write,
        });

        gb->render_pass = vk::render_pass_builder_t::prepare(backend->device->handle)
                              .unwrap()
                              .clear_color({ 0.0f, 0.0f, 0.0f, 1.0f })
                              .build(gb->subpasses, gb->attachments)
                              .unwrap();

        gb->create_views(*backend);
        gb->create_fbs(*backend);

        const path vs_path { SAMPLE_DIR "main.vs.glsl" };
        const path fs_path { SAMPLE_DIR "main.fs.glsl" };

        vk::spirv_compiler_t compiler;
        compiler.option_target_env(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2)
            .option_generate_debug_info()
            .option_target_spirv(shaderc_spirv_version_1_3)
            .option_source_language(shaderc_source_language_glsl)
            .option_optimization_level(shaderc_optimization_level_zero)
            .option_warnings_as_errors();

        fmt::println("- Reading shader files");
        auto vs_content = vs_path.read_file().unwrap();
        auto fs_content = fs_path.read_file().unwrap();

        fmt::println("- Creating shader modules");
        gb->vs_shader_module = vk::shader_module_builder_t::prepare(backend->device.getmut(), &compiler)
                                   .unwrap()
                                   .kind(vk::shader_kind::glsl_vertex)
                                   .entry_point("main")
                                   .content(std::move(vs_content))
                                   .build()
                                   .unwrap();

        gb->fs_shader_module = vk::shader_module_builder_t::prepare(backend->device.getmut(), &compiler)
                                   .unwrap()
                                   .kind(vk::shader_kind::glsl_fragment)
                                   .entry_point("main")
                                   .content(std::move(fs_content))
                                   .build()
                                   .unwrap();

        struct vertex_t
        {
            std::array<float, 2> pos;
            std::array<float, 3> col;
        };

        fmt::println("- Creating graphics pipeline");
        gb->pipeline = vk::pipeline_builder_t ::prepare(backend->device.getmut())
                           .unwrap()
                           ->shader_stages()
                           .stage(gb->vs_shader_module, vk::shader_stage_flag::vertex, "main")
                           .stage(gb->fs_shader_module, vk::shader_stage_flag::fragment, "main")
                           .dynamic_states()
                           .dynamic_state(vk::dynamic_state::viewport)
                           .dynamic_state(vk::dynamic_state::scissor)
                           .vertex_input()
                           .template binding<vertex_t>(0, vk::vertex_input_rate::vertex)
                           .attribute(0, offsetof(vertex_t, pos), vk::vertex_format::vec2_t)
                           .attribute(1, offsetof(vertex_t, col), vk::vertex_format::vec3_t)
                           .input_assembly()
                           .viewport_states()
                           .viewport(0.0f, 0.0f, (f32)backend->swapchain->width, (f32)backend->swapchain->height, 0.0f, 1.0f)
                           .scissor(0.0f, 0.0f, backend->swapchain->width, backend->swapchain->height)
                           .rasterizer()
                           .multisample()
                           .color_blending()
                           .new_color_blend_attachment()
                           .end_attachment()
                           .desc_set_layout()
                           .pipeline_layout()
                           .prepare_pipeline()
                           .render_pass(gb->render_pass.getmut())
                           .subpass(0)
                           .build()
                           .unwrap();

        fmt::println("- Creating synchronization objects");
        // Synchronization
        gb->sync_objects = vk::sync_objects_builder_t::prepare(backend->device.getmut())
                               .unwrap()
                               .semaphores(max_frames_in_flight + backend->swapchain->images.size())
                               .fences(max_frames_in_flight)
                               .build()
                               .unwrap();

        fmt::println("- Creating command pool and command buffers");
        gb->graphics_cmd_pool = vk::cmd_pool_builder_t::prepare(backend->device.getmut(), backend->graphics_qf->index)
                                    .unwrap()
                                    .flag(vk::command_pool_create_flag::reset_command_buffer)
                                    .build()
                                    .unwrap();

        gb->transfer_cmd_pool = vk::cmd_pool_builder_t::prepare(backend->device.getmut(), backend->transfer_qf->index)
                                    .unwrap()
                                    .flag(vk::command_pool_create_flag::reset_command_buffer)
                                    .build()
                                    .unwrap();

        fmt::println("- Creating command buffers");
        gb->draw_cmds = gb->graphics_cmd_pool->alloc_cmds(max_frames_in_flight).unwrap();

        std::vector<vertex_t> vertices = {
            { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
            {  { 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
            {   { 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
            {  { -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } }
        };

        std::vector<ui16> indices = { 0, 1, 2, 2, 3, 0 };

        fmt::println("- Creating vertex buffer");
        gb->vertex_buffer = vk::vertex_buffer_builder_t::prepare(backend->device.getmut())
                                .unwrap()
                                .template vertices<vertex_t>(vertices)
                                .buffer_usage_flag(vk::buffer_usage_flag::transfer_destination)
                                .memory_flags(vk::memory_flag::dedicated_memory)
                                .build()
                                .unwrap();

        fmt::println("- Creating index buffer");
        gb->index_buffer = vk::index_buffer_builder_t::prepare(backend->device.getmut())
                               .unwrap()
                               .indices(std::span<const ui16> { indices })
                               .buffer_usage_flag(vk::buffer_usage_flag::transfer_destination)
                               .memory_flags(vk::memory_flag::dedicated_memory)
                               .build()
                               .unwrap();

        fmt::println("- Creating staging buffer");
        auto staging_buffer = vk::staging_buffer_builder_t::prepare(backend->device.getmut(), gb->vertex_buffer.size)
                                  .unwrap()
                                  .build()
                                  .unwrap();

        fmt::println("- Copying vertices to staging buffer");
        staging_buffer.transfer(vertices.data(), sizeof(vertex_t) * vertices.size()).unwrap();

        fmt::println("- Copying staging buffer to vertex buffer");
        auto cpy_cmd = gb->transfer_cmd_pool->alloc_cmds(1).unwrap().get(0).unwrap();

        cpy_cmd.begin_one_time().unwrap();
        cpy_cmd.copy_buffer(staging_buffer.buffer, gb->vertex_buffer.buffer, gb->vertex_buffer.size);
        cpy_cmd.end().unwrap();

        fmt::println("- Submitting copy command buffer");
        vk::submit_helper_t::prepare()
            .cmd_buffer(&cpy_cmd.handle)
            .wait_stage(vk::pipeline_stage_flag::transfer)
            .submit(backend->transfer_qf->queues.front())
            .unwrap();

        backend->device->wait().unwrap();

        fmt::println("- Copying indices to staging buffer");
        staging_buffer.transfer(indices.data(), sizeof(ui16) * indices.size()).unwrap();

        fmt::println("- Copying staging buffer to index buffer");
        cpy_cmd = gb->transfer_cmd_pool->alloc_cmds(1).unwrap().get(0).unwrap();

        cpy_cmd.begin_one_time().unwrap();
        cpy_cmd.copy_buffer(staging_buffer.buffer, gb->index_buffer.buffer, gb->index_buffer.size);
        cpy_cmd.end().unwrap();

        fmt::println("- Submitting copy command buffer");
        vk::submit_helper_t::prepare()
            .cmd_buffer(&cpy_cmd.handle)
            .wait_stage(vk::pipeline_stage_flag::transfer)
            .submit(backend->transfer_qf->queues.front())
            .unwrap();

        backend->device->wait().unwrap();

        return gb;
    }
} // namespace

auto main() -> int
{
    try
    {
        auto backend     = initialize_backend();
        auto gui_backend = initialize_gui_backend(backend);

        ui32 frame = 0;

        fmt::println("- Main loop");
        while (!backend->window->should_close())
        {
            backend->glfw_driver->poll_events();

            if (backend->window->minimized())
            {
                using namespace std::literals;
                std::this_thread::sleep_for(orb::milliseconds_t(100));
                continue;
            }

            auto fences         = gui_backend->sync_objects.fences(frame, 1);
            auto img_avail_sems = gui_backend->sync_objects.semaphores(frame, 1);

            // Wait fences
            fences.wait().unwrap();

            // Acquire the next swapchain image
            auto res = vk::acquire_img(*backend->swapchain, img_avail_sems.handles.back(), nullptr);

            if (res.require_sc_rebuild())
            {
                backend->device->wait().unwrap();
                backend->swapchain->rebuild().unwrap();

                gui_backend->create_views(*backend);
                gui_backend->create_fbs(*backend);
                continue;
            }
            else if (res.is_error())
            {
                fmt::println("Acquire img error");
                return 1;
            }

            // Reset fences
            fences.reset().unwrap();

            uint32_t img_index = res.img_index();

            auto render_finished_sems = gui_backend->sync_objects.semaphores(img_index + max_frames_in_flight, 1);

            // Render to the framebuffer
            gui_backend->render_pass->begin_info.framebuffer       = gui_backend->fbs.handles[img_index];
            gui_backend->render_pass->begin_info.renderArea.extent = backend->swapchain->extent;

            // Begin command buffer recording
            auto cmd = gui_backend->draw_cmds.get(frame).unwrap();
            cmd.begin_one_time().unwrap();

            // Begin the render pass
            gui_backend->render_pass->begin(cmd.handle);

            // Bind the graphics pipeline
            vkCmdBindPipeline(cmd.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, gui_backend->pipeline->handle);
            std::array<VkDeviceSize, 1> offsets = { 0 };
            vkCmdBindVertexBuffers(cmd.handle, 0, 1, &gui_backend->vertex_buffer.buffer, offsets.data());
            vkCmdBindIndexBuffer(cmd.handle, gui_backend->index_buffer.buffer, 0, gui_backend->index_buffer.index_type);

            // Set viewport and scissor
            auto& viewport        = gui_backend->pipeline->viewports.back();
            auto& scissor         = gui_backend->pipeline->scissors.back();
            viewport.width        = static_cast<f32>(backend->swapchain->width);
            viewport.height       = static_cast<f32>(backend->swapchain->height);
            scissor.extent.width  = backend->swapchain->width;
            scissor.extent.height = backend->swapchain->height;
            vkCmdSetViewport(cmd.handle, 0, 1, &viewport);
            vkCmdSetScissor(cmd.handle, 0, 1, &scissor);

            // Draw quad
            vkCmdDrawIndexed(cmd.handle, 6, 1, 0, 0, 0);

            // End the render pass
            gui_backend->render_pass->end(cmd.handle);

            // End command buffer recording
            cmd.end().unwrap();

            // Submit render
            vk::submit_helper_t::prepare()
                .wait_semaphores(img_avail_sems.handles)
                .signal_semaphores(render_finished_sems.handles)
                .cmd_buffer(&cmd.handle)
                .wait_stage(vk::pipeline_stage_flag::color_attachment_output)
                .submit(backend->graphics_qf->queues.front(), fences.handles.back())
                .unwrap();

            // Present the rendered image
            auto present_res = vk::present_helper_t::prepare()
                                   .swapchain(*backend->swapchain)
                                   .wait_semaphores(render_finished_sems.handles)
                                   .img_index(img_index)
                                   .present(backend->graphics_qf->queues.front());

            if (present_res.require_sc_rebuild())
            {
                continue;
            }
            else if (present_res.is_error())
            {
                fmt::println("Frame present error: {}", vk::vkres::get_repr(present_res.error()));
                return 1;
            }

            frame = (frame + 1) % max_frames_in_flight;
        }

        backend->device->wait().unwrap();
    }
    catch (const orb::exception& e)
    {
        fmt::println("Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
