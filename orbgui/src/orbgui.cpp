#include <orb/renderer.hpp>

#include "orb/vk/all.hpp"
#include "orbgui/orbgui.hpp"

namespace orb::gui
{
    static constexpr ui32 max_frames_in_flight = 2;

    struct gui_renderer_t
    {
        // device
        weak<vk::device_t>  device;
        box<vk::cmd_pool_t> graphics_cmd_pool;
        box<vk::cmd_pool_t> transfer_cmd_pool;
        vk::cmd_buffers_t   draw_cmds;
        VkQueue             graphics_queue;
        VkQueue             transfer_queue;

        // render pass
        box<vk::render_pass_t> render_pass;
        vk::attachments_t      attachments;
        vk::subpasses_t        subpasses;
        vk::images_t           images;
        vk::views_t            views;
        vk::framebuffers_t     fbs;
        VkExtent2D             extent;

        // graphics pipeline
        vk::shader_module_t          vs_shader_module;
        vk::shader_module_t          fs_shader_module;
        box<vk::graphics_pipeline_t> pipeline;
        vk::vertex_buffer_t          vertex_buffer;
        vk::index_buffer_t           index_buffer;

        // render info
        ui32                  frame = 0;
        vk::semaphores_t      render_finished;
        vk::semaphores_view_t finished;

        auto create_surfaces() -> orb::result<void>
        {
            if (auto res = this->create_images(); !res)
            {
                return res;
            }

            if (auto res = this->create_views(); !res)
            {
                return res;
            }

            return this->create_fbs();
        }

        auto create_images() -> orb::result<void>
        {
            auto res = vk::images_builder_t::prepare(this->device->allocator)
                           .unwrap()
                           .count(max_frames_in_flight)
                           .usage(vk::image_usage_flag::color_attachment)
                           .usage(vk::image_usage_flag::transfer_src)
                           .size(extent.width, extent.height)
                           .format(vk::format::b8g8r8a8_unorm)
                           .mem_usage(vk::memory_usage::usage_auto)
                           .mem_flags(vk::memory_flag::dedicated_memory)
                           .build();

            if (!res)
            {
                return res.error();
            }

            this->images = std::move(res.unwrap());

            return {};
        }

        auto create_views() -> orb::result<void>
        {
            auto res = vk::views_builder_t::prepare(this->device->handle)
                           .unwrap()
                           .images(this->images.handles)
                           .aspect_mask(vk::image_aspect_flag::color)
                           .format(vk::format::b8g8r8a8_unorm)
                           .build();

            if (!res)
            {
                return res.error();
            }

            this->views = std::move(res.unwrap());

            return {};
        };

        auto create_fbs() -> orb::result<void>
        {
            auto res = vk::framebuffers_builder_t::prepare(device, render_pass->handle)
                           .unwrap()
                           .size(extent.width, extent.height)
                           .attachments(views.handles)
                           .build();

            if (!res)
            {
                return res.error();
            }

            this->fbs = std::move(res.unwrap());

            return {};
        };

        void render()
        {
            // Render to the framebuffer
            this->render_pass->begin_info.framebuffer       = this->fbs.handles[this->frame];
            this->render_pass->begin_info.renderArea.extent = this->extent;

            // Begin command buffer recording
            auto cmd = this->draw_cmds.get(this->frame).unwrap();
            cmd.begin_one_time().unwrap();

            // Begin the render pass
            this->render_pass->begin(cmd.handle);

            // Bind the graphics pipeline
            vkCmdBindPipeline(cmd.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline->handle);
            std::array<VkDeviceSize, 1> offsets = { 0 };
            vkCmdBindVertexBuffers(cmd.handle, 0, 1, &this->vertex_buffer.buffer, offsets.data());
            vkCmdBindIndexBuffer(cmd.handle, this->index_buffer.buffer, 0, this->index_buffer.index_type);

            // Set viewport and scissor
            auto& viewport        = this->pipeline->viewports.back();
            auto& scissor         = this->pipeline->scissors.back();
            viewport.width        = static_cast<f32>(this->extent.width);
            viewport.height       = static_cast<f32>(this->extent.height);
            scissor.extent.width  = this->extent.width;
            scissor.extent.height = this->extent.height;
            vkCmdSetViewport(cmd.handle, 0, 1, &viewport);
            vkCmdSetScissor(cmd.handle, 0, 1, &scissor);

            // Draw quad
            vkCmdDrawIndexed(cmd.handle, 6, 1, 0, 0, 0);

            // End the render pass
            this->render_pass->end(cmd.handle);

            // End command buffer recording
            cmd.end().unwrap();

            this->finished = this->render_finished.view(this->frame, 1);

            // Submit render
            vk::submit_helper_t::prepare()
                .signal_semaphores(this->finished.handles)
                .cmd_buffer(&cmd.handle)
                .submit(this->graphics_queue)
                .unwrap();

            frame = (frame + 1) % max_frames_in_flight;
        }
    };

    instance_t::~instance_t() = default;

    auto instance_t::create(instance_create_info_t&& info) -> orb::result<instance_t>
    {
        auto r = make_box<gui_renderer_t>();

        r->device         = info.device;
        r->graphics_queue = info.graphics_queue;
        r->transfer_queue = info.transfer_queue;

        r->extent = {
            .width  = info.extent_width,
            .height = info.extent_height,
        };

        r->attachments.add({
            .img_format        = vkenum(vk::format::b8g8r8a8_unorm),
            .samples           = vk::sample_count_flag::_1,
            .load_ops          = vk::attachment_load_op::clear,
            .store_ops         = vk::attachment_store_op::store,
            .stencil_load_ops  = vk::attachment_load_op::dont_care,
            .stencil_store_ops = vk::attachment_store_op::dont_care,
            .initial_layout    = vk::image_layout::undefined,
            .final_layout      = vk::image_layout::transfer_src_optimal,
            .attachment_layout = vk::image_layout::color_attachment_optimal,
        });

        const auto [color_descs, color_refs] = r->attachments.spans(0, 1);

        r->subpasses.add_subpass({
            .bind_point = vk::pipeline_bind_point::graphics,
            .color_refs = color_refs,
        });

        r->subpasses.add_dependency({
            .src        = vk::subpass_external,
            .dst        = 0,
            .src_stage  = vk::pipeline_stage_flag::color_attachment_output,
            .dst_stage  = vk::pipeline_stage_flag::color_attachment_output,
            .src_access = 0,
            .dst_access = vk::access_flag::color_attachment_write,
        });

        r->render_pass = vk::render_pass_builder_t::prepare(info.device->handle)
                             .unwrap()
                             .clear_color({ 0.0f, 0.0f, 0.0f, 1.0f })
                             .build(r->subpasses, r->attachments)
                             .unwrap();

        r->create_surfaces();

        const path vs_path { "/home/lucla/work/OrbGui/samples/minimal/main.vs.glsl" };
        const path fs_path { "/home/lucla/work/OrbGui/samples/minimal/main.fs.glsl" };

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
        r->vs_shader_module = vk::shader_module_builder_t::prepare(info.device, &compiler)
                                  .unwrap()
                                  .kind(vk::shader_kind::glsl_vertex)
                                  .entry_point("main")
                                  .content(std::move(vs_content))
                                  .build()
                                  .unwrap();

        r->fs_shader_module = vk::shader_module_builder_t::prepare(info.device, &compiler)
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
        r->pipeline = vk::pipeline_builder_t ::prepare(info.device)
                          .unwrap()
                          ->shader_stages()
                          .stage(r->vs_shader_module, vk::shader_stage_flag::vertex, "main")
                          .stage(r->fs_shader_module, vk::shader_stage_flag::fragment, "main")
                          .dynamic_states()
                          .dynamic_state(vk::dynamic_state::viewport)
                          .dynamic_state(vk::dynamic_state::scissor)
                          .vertex_input()
                          .binding<vertex_t>(0, vk::vertex_input_rate::vertex)
                          .attribute(0, offsetof(vertex_t, pos), vk::vertex_format::vec2_t)
                          .attribute(1, offsetof(vertex_t, col), vk::vertex_format::vec3_t)
                          .input_assembly()
                          .viewport_states()
                          .viewport(0.0f, 0.0f, (f32)r->extent.width, (f32)r->extent.height, 0.0f, 1.0f)
                          .scissor(0.0f, 0.0f, r->extent.width, r->extent.height)
                          .rasterizer()
                          .multisample()
                          .color_blending()
                          .new_color_blend_attachment()
                          .end_attachment()
                          .desc_set_layout()
                          .pipeline_layout()
                          .prepare_pipeline()
                          .render_pass(r->render_pass.getmut())
                          .subpass(0)
                          .build()
                          .unwrap();

        fmt::println("- Creating command pool and command buffers");
        r->graphics_cmd_pool = vk::cmd_pool_builder_t::prepare(info.device, info.graphics_qf)
                                   .unwrap()
                                   .flag(vk::command_pool_create_flag::reset_command_buffer)
                                   .build()
                                   .unwrap();

        r->transfer_cmd_pool = vk::cmd_pool_builder_t::prepare(info.device, info.transfer_qf)
                                   .unwrap()
                                   .flag(vk::command_pool_create_flag::reset_command_buffer)
                                   .build()
                                   .unwrap();

        fmt::println("- Creating command buffers");
        r->draw_cmds = r->graphics_cmd_pool->alloc_cmds(max_frames_in_flight).unwrap();

        std::vector<vertex_t> vertices = {
            { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
            {  { 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
            {   { 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
            {  { -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f } }
        };

        std::vector<ui16> indices = { 0, 1, 2, 2, 3, 0 };

        fmt::println("- Creating vertex buffer");
        r->vertex_buffer = vk::vertex_buffer_builder_t::prepare(info.device)
                               .unwrap()
                               .vertices(vertices)
                               .buffer_usage_flag(vk::buffer_usage_flag::transfer_destination)
                               .memory_flags(vk::memory_flag::dedicated_memory)
                               .build()
                               .unwrap();

        fmt::println("- Creating index buffer");
        r->index_buffer = vk::index_buffer_builder_t::prepare(info.device)
                              .unwrap()
                              .indices(std::span<const ui16> { indices })
                              .buffer_usage_flag(vk::buffer_usage_flag::transfer_destination)
                              .memory_flags(vk::memory_flag::dedicated_memory)
                              .build()
                              .unwrap();

        fmt::println("- Creating staging buffer");
        auto staging_buffer = vk::staging_buffer_builder_t::prepare(info.device, r->vertex_buffer.size)
                                  .unwrap()
                                  .build()
                                  .unwrap();

        fmt::println("- Copying vertices to staging buffer");
        staging_buffer.transfer(vertices.data(), sizeof(vertex_t) * vertices.size()).unwrap();

        fmt::println("- Copying staging buffer to vertex buffer");
        auto cpy_cmd = r->transfer_cmd_pool->alloc_cmds(1).unwrap().get(0).unwrap();

        cpy_cmd.begin_one_time().unwrap();
        cpy_cmd.copy_buffer(staging_buffer.buffer, r->vertex_buffer.buffer, r->vertex_buffer.size);
        cpy_cmd.end().unwrap();

        fmt::println("- Submitting copy command buffer");
        vk::submit_helper_t::prepare()
            .cmd_buffer(&cpy_cmd.handle)
            .submit(info.transfer_queue)
            .unwrap();

        info.device->wait().unwrap();

        fmt::println("- Copying indices to staging buffer");
        staging_buffer.transfer(indices.data(), sizeof(ui16) * indices.size()).unwrap();

        fmt::println("- Copying staging buffer to index buffer");
        cpy_cmd = r->transfer_cmd_pool->alloc_cmds(1).unwrap().get(0).unwrap();

        cpy_cmd.begin_one_time().unwrap();
        cpy_cmd.copy_buffer(staging_buffer.buffer, r->index_buffer.buffer, r->index_buffer.size);
        cpy_cmd.end().unwrap();

        fmt::println("- Submitting copy command buffer");
        vk::submit_helper_t::prepare()
            .cmd_buffer(&cpy_cmd.handle)
            .submit(info.transfer_queue)
            .unwrap();

        info.device->wait().unwrap();

        fmt::println("- Creating render finished semaphores");
        r->render_finished = vk::semaphores_builder_t::prepare(info.device)
                                 .unwrap()
                                 .count(max_frames_in_flight)
                                 .stage(vk::pipeline_stage_flag::transfer)
                                 .build()
                                 .unwrap();

        return instance_t { std::move(r) };
    }

    auto instance_t::render() -> orb::result<void>
    {
        // Render to the framebuffer
        auto& r                                      = this->m_renderer;
        r->render_pass->begin_info.framebuffer       = r->fbs.handles[r->frame];
        r->render_pass->begin_info.renderArea.extent = r->extent;

        // Begin command buffer recording
        auto cmd = r->draw_cmds.get(r->frame).unwrap();
        cmd.begin_one_time().unwrap();

        // Begin the render pass
        r->render_pass->begin(cmd.handle);

        // Bind the graphics pipeline
        vkCmdBindPipeline(cmd.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline->handle);
        std::array<VkDeviceSize, 1> offsets = { 0 };
        vkCmdBindVertexBuffers(cmd.handle, 0, 1, &r->vertex_buffer.buffer, offsets.data());
        vkCmdBindIndexBuffer(cmd.handle, r->index_buffer.buffer, 0, r->index_buffer.index_type);

        // Set viewport and scissor
        auto& viewport        = r->pipeline->viewports.back();
        auto& scissor         = r->pipeline->scissors.back();
        viewport.width        = static_cast<f32>(r->extent.width);
        viewport.height       = static_cast<f32>(r->extent.height);
        scissor.extent.width  = r->extent.width;
        scissor.extent.height = r->extent.height;
        vkCmdSetViewport(cmd.handle, 0, 1, &viewport);
        vkCmdSetScissor(cmd.handle, 0, 1, &scissor);

        // Draw quad
        vkCmdDrawIndexed(cmd.handle, 6, 1, 0, 0, 0);

        // End the render pass
        r->render_pass->end(cmd.handle);

        // End command buffer recording
        cmd.end().unwrap();

        r->finished = r->render_finished.view(r->frame, 1);

        // Submit render
        vk::submit_helper_t::prepare()
            .signal_semaphores(r->finished.handles)
            .cmd_buffer(&cmd.handle)
            .submit(r->graphics_queue)
            .unwrap();

        r->frame = (r->frame + 1) % max_frames_in_flight;
        return {};
    }

    auto instance_t::on_resize() -> orb::result<void>
    {
        return this->m_renderer->create_surfaces();
    }

    instance_t::instance_t(orb::box<gui_renderer_t> renderer)
        : m_renderer(std::move(renderer))
    {
    }

    auto instance_t::rendered_image() const -> VkImage
    {
        return this->m_renderer->images.handles[this->m_renderer->frame];
    }

    auto instance_t::render_finished() -> vk::semaphores_view_t&
    {
        return this->m_renderer->finished;
    }
} // namespace orb::gui
