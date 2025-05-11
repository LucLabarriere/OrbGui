#include <span>
#include <thread>

#include <orb/eval.hpp>
#include <orb/files.hpp>
#include <orb/flux.hpp>
#include <orb/renderer.hpp>
#include <orb/time.hpp>

#include "sample.hpp"

#include <orbgui/orbgui.hpp>

using namespace orb;

auto main() -> int
{
    try
    {
        auto sample = sample_t::create().unwrap();

        auto gui_backend = orb::gui::instance_t::create(sample.get_gui_create_info())
                               .unwrap();

        while (!sample.window_should_close())
        {
            sample.begin_loop_step().unwrap();

            if (sample.is_resize_required())
            {
                gui_backend.on_resize().unwrap();
                continue;
            }

            gui_backend.render();

            sample.end_loop_step(gui_backend.rendered_image(), gui_backend.render_finished()).unwrap();

            if (sample.is_resize_required())
            {
                gui_backend.on_resize().unwrap();
                continue;
            }
        }

        sample.terminate().unwrap();
    }
    catch (const orb::exception& e)
    {
        fmt::println("Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
