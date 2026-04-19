#include "Windows.Xbox.Media.GameTransportControlsPropertyChangedEventArgs.h"
#include "WinDurangoWinRT.h"

namespace winrt::Windows::Xbox::Media::implementation
{
    GameTransportControlsProperty GameTransportControlsPropertyChangedEventArgs::Property()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: Property");
        return GameTransportControlsProperty::Unknown;
    }
} // namespace winrt::Windows::Xbox::Media::implementation