#include <orbgui/core.hpp>
#include <orb/print.hpp>

#include "sample.hpp"

auto main() -> int {
    auto sample = sample_t::create().unwrap();

    while (!sample.window_should_close())
    {
        sample.begin_loop_step().unwrap();
        sample.end_loop_step().unwrap();
    }

    sample.terminate().unwrap();

    return 0;
}
