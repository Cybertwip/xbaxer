#pragma once
#include "Windows.Xbox.Media.GameTransportControlsButtonPressedEventArgs.g.h"
#include <Windows.h>

namespace winrt::Windows::Xbox::Media::implementation
{
    struct GameTransportControlsButtonPressedEventArgs
        : GameTransportControlsButtonPressedEventArgsT<GameTransportControlsButtonPressedEventArgs>
    {
        GameTransportControlsButtonPressedEventArgs() = default;
        GameTransportControlsButton Button();
    };
} // namespace winrt::Windows::Xbox::Media::implementation