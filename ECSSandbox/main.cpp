#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

using namespace ankerl::nanobench;
int main()
{
    double d = 1.0;
    Bench().run("some double ops", [&] {
        d += 1.0 / d;
        if (d > 5.0) {
            d -= 5.0;
        }
        doNotOptimizeAway(d);
        });

	return 0;
}