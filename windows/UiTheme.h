#pragma once

#include <windows.h>

#include <cstdint>

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>

// 面板配色。WinUI 控件自身的配色跟随系统主题；这里只负责面板/卡片等自绘部分。
struct UiPalette
{
    winrt::Windows::UI::Color PanelBackground;
    winrt::Windows::UI::Color PanelBorder;
    winrt::Windows::UI::Color Divider;
    winrt::Windows::UI::Color PrimaryText;
    winrt::Windows::UI::Color SecondaryText;
    winrt::Windows::UI::Color CardBackground;
    winrt::Windows::UI::Color CardBorder;
    winrt::Windows::UI::Color Accent;
    winrt::Windows::UI::Color AccentFill;
    winrt::Windows::UI::Color Online;
    winrt::Windows::UI::Color Offline;
    winrt::Windows::UI::Color Unknown;
};

namespace Ui
{
    inline winrt::Windows::UI::Color Rgb(uint8_t red, uint8_t green, uint8_t blue)
    {
        return winrt::Windows::UI::Color{ 255, red, green, blue };
    }

    inline winrt::Windows::UI::Color Rgba(uint8_t alpha, uint8_t red, uint8_t green, uint8_t blue)
    {
        return winrt::Windows::UI::Color{ alpha, red, green, blue };
    }

    inline winrt::Microsoft::UI::Xaml::Media::SolidColorBrush Brush(winrt::Windows::UI::Color const& color)
    {
        return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(color);
    }

    inline bool SystemUsesLightTheme()
    {
        DWORD light = 1;
        DWORD size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        {
            light = 1;
        }
        return light != 0;
    }

    inline UiPalette Palette(bool dark)
    {
        if (!dark)
        {
            return UiPalette{
                Rgb(0xF8, 0xF8, 0xF9),       // PanelBackground
                Rgba(0x1A, 0x00, 0x00, 0x00), // PanelBorder
                Rgba(0x17, 0x00, 0x00, 0x00), // Divider
                Rgb(0x1A, 0x1A, 0x1A),       // PrimaryText
                Rgb(0x5F, 0x5F, 0x5F),       // SecondaryText
                Rgb(0xFF, 0xFF, 0xFF),       // CardBackground
                Rgb(0xE0, 0xE0, 0xE2),       // CardBorder
                Rgb(0x0F, 0x6C, 0xBD),       // Accent
                Rgb(0xE8, 0xF1, 0xFC),       // AccentFill
                Rgb(0x0F, 0x7B, 0x0F),       // Online
                Rgb(0xC4, 0x2B, 0x1C),       // Offline
                Rgb(0x78, 0x78, 0x78),       // Unknown
            };
        }
        return UiPalette{
            Rgb(0x2B, 0x2B, 0x2E),
            Rgba(0x26, 0xFF, 0xFF, 0xFF),
            Rgba(0x22, 0xFF, 0xFF, 0xFF),
            Rgb(0xF2, 0xF2, 0xF2),
            Rgb(0xA8, 0xA8, 0xAE),
            Rgb(0x32, 0x32, 0x36),
            Rgb(0x45, 0x45, 0x4A),
            Rgb(0x4C, 0xC2, 0xFF),
            Rgb(0x1F, 0x3A, 0x5F),
            Rgb(0x6C, 0xCB, 0x5F),
            Rgb(0xFF, 0x99, 0xA4),
            Rgb(0xA0, 0xA0, 0xA6),
        };
    }
}
