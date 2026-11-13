#pragma once

#include <algorithm>

namespace PanelPlacement
{
    struct Rect
    {
        int left;
        int top;
        int right;
        int bottom;
    };

    struct Position
    {
        int x;
        int y;
    };

    struct Anchor
    {
        Rect workArea;
        bool alignRight;
        bool alignBottom;
    };

    // 以任务栏所在边缘为锚点，隐藏图标抽屉的坐标不参与定位。
    inline Anchor FromMonitor(Rect monitor, Rect workArea)
    {
        bool taskbarOnLeft = workArea.left > monitor.left;
        bool taskbarOnTop = workArea.top > monitor.top;
        return { workArea, !taskbarOnLeft, !taskbarOnTop };
    }

    inline Position Place(Anchor anchor, int width, int height, int margin)
    {
        auto work = anchor.workArea;
        int maxX = std::max(work.left, work.right - width);
        int maxY = std::max(work.top, work.bottom - height);
        int x = anchor.alignRight ? work.right - width - margin : work.left + margin;
        int y = anchor.alignBottom ? work.bottom - height - margin : work.top + margin;
        return { std::clamp(x, work.left, maxX), std::clamp(y, work.top, maxY) };
    }
}
