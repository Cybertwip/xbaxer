#pragma once
#include "Windows.Xbox.Media.GameTransportControlsPropertyChangedEventArgs.g.h"
#include <Windows.h>

namespace winrt::Windows::Xbox::Media::implementation
{
    struct GameTransportControlsPropertyChangedEventArgs
        : GameTransportControlsPropertyChangedEventArgsT<GameTransportControlsPropertyChangedEventArgs>
    {
        GameTransportControlsPropertyChangedEventArgs() = default;
        GameTransportControlsProperty Property();
    };
} // namespace winrt::Windows::Xbox::Media::implementation