#include "Windows.Xbox.Media.GameTransportControlsButtonPressedEventArgs.h"
#include "WinDurangoWinRT.h"

namespace winrt::Windows::Xbox::Media::implementation
{
    GameTransportControlsButton GameTransportControlsButtonPressedEventArgs::Button()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: Button");
        return GameTransportControlsButton::Play;
    }
} // namespace winrt::Windows::Xbox::Media::implementation