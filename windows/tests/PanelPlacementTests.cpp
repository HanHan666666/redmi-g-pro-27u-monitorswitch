#include "../PanelPlacement.h"
#include <cstdio>

using namespace PanelPlacement;

int main()
{
    struct Case
    {
        char const* name;
        Rect monitor;
        Rect work;
        int width, height, margin;
        int x, y;
    };
    Case cases[] = {
        { "bottom taskbar", { 0, 0, 1920, 1080 }, { 0, 0, 1920, 1040 }, 360, 520, 8, 1552, 512 },
        { "top taskbar", { 0, 0, 1920, 1080 }, { 0, 40, 1920, 1080 }, 360, 520, 8, 1552, 48 },
        { "left taskbar", { 0, 0, 1920, 1080 }, { 48, 0, 1920, 1080 }, 360, 520, 8, 56, 552 },
        { "right taskbar", { 0, 0, 1920, 1080 }, { 0, 0, 1872, 1080 }, 360, 520, 8, 1504, 552 },
        { "negative monitor coordinates", { -1920, -100, 0, 980 }, { -1920, -100, 0, 940 }, 360, 520, 8, -368, 412 },
        { "small work area", { 0, 0, 200, 200 }, { 0, 0, 200, 150 }, 360, 520, 8, 0, 0 },
        { "scaled panel", { 0, 0, 3840, 2160 }, { 0, 0, 3840, 2048 }, 540, 780, 12, 3288, 1256 },
    };
    for (auto test : cases)
    {
        auto position = Place(FromMonitor(test.monitor, test.work), test.width, test.height, test.margin);
        if (position.x != test.x || position.y != test.y)
        {
            std::fprintf(stderr, "%s: got (%d,%d), expected (%d,%d)\n", test.name, position.x, position.y, test.x, test.y);
            return 1;
        }
    }
    std::puts("Panel placement: 7 cases passed");
}
