#include "Windows.Xbox.Media.GameTransportControls.h"
#include "WinDurangoWinRT.h"

namespace winrt::Windows::Xbox::Media::implementation
{
    hstring GameTransportControls::Title()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: Title _out_");
        return _title;
    }

    void GameTransportControls::Title(hstring const &value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: Title _in_");
        _title = value;
    }

    hstring GameTransportControls::Subtitle()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: Subtitle _out_");
        return _subtitle;
    }

    void GameTransportControls::Subtitle(hstring const &value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: Subtitle _in_");
        _subtitle = value;
    }

    winrt::Windows::Xbox::Media::GamePlaybackStatus GameTransportControls::PlaybackStatus()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: PlaybackStatus _out_");
        return _playbackStatus;
    }

    void GameTransportControls::PlaybackStatus(winrt::Windows::Xbox::Media::GamePlaybackStatus const &value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: PlaybackStatus _in_");
        _playbackStatus = value;
    }

    Windows::Xbox::Media::SoundLevel GameTransportControls::SoundLevel()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: SoundLevel");
        return Windows::Xbox::Media::SoundLevel::Unknown;
    }

    bool GameTransportControls::IsEnabled()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsEnabled _out_");
        return _isEnabled;
    }

    void GameTransportControls::IsEnabled(bool value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsEnabled _in_");
        _isEnabled = value;
    }

    bool GameTransportControls::IsPlayEnabled()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsPlayEnabled _out_");
        return _isPlayEnabled;
    }

    void GameTransportControls::IsPlayEnabled(bool value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsPlayEnabled _in_");
        _isPlayEnabled = value;
    }

    bool GameTransportControls::IsPauseEnabled()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsPauseEnabled _out_");
        return _isPauseEnabled;
    }

    void GameTransportControls::IsPauseEnabled(bool value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsPauseEnabled _in_");
        _isPauseEnabled = value;
    }

    bool GameTransportControls::IsMenuEnabled()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsMenuEnabled _out_");
        return _isMenuEnabled;
    }

    void GameTransportControls::IsMenuEnabled(bool value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsMenuEnabled _in_");
        _isMenuEnabled = value;
    }

    bool GameTransportControls::IsViewEnabled()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsViewEnabled _out_");
        return _isViewEnabled;
    }

    void GameTransportControls::IsViewEnabled(bool value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsViewEnabled _in_");
        _isViewEnabled = value;
    }

    bool GameTransportControls::IsBackEnabled()
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsBackEnabled _out_");
        return _isBackEnabled;
    }

    void GameTransportControls::IsBackEnabled(bool value)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: IsBackEnabled _in_");
        _isBackEnabled = value;
    }

    /*
     * Dont use EventRegistrationToken
     * https://stackoverflow.com/questions/63346461/namespace-winrtwindowsfoundationeventregistrationtoken-has-no-member-ev
     */
    winrt::event_token GameTransportControls::add_ButtonPressed(
        Windows::Foundation::TypedEventHandler<
            winrt::Windows::Xbox::Media::GameTransportControls,
            winrt::Windows::Xbox::Media::GameTransportControlsButtonPressedEventArgs> const &handler)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: add_ButtonPressed");
        return e_ButtonPressedHandler.add(handler);
    }

    void GameTransportControls::remove_ButtonPressed(winrt::event_token const &token)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: remove_ButtonPressed");
        e_ButtonPressedHandler.remove(token);
    }

    winrt::event_token GameTransportControls::add_PropertyChanged(
        Windows::Foundation::TypedEventHandler<
            winrt::Windows::Xbox::Media::GameTransportControls,
            winrt::Windows::Xbox::Media::GameTransportControlsPropertyChangedEventArgs> const &handler)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: add_PropertyChanged");
        return e_PropertyChangedHandler.add(handler);
    }

    void GameTransportControls::remove_PropertyChanged(winrt::event_token const &token)
    {
        p_wd->log.Warn("WinDurango::WinRT::Windows::Xbox::Media", "Unimplemented: remove_PropertyChanged");
        e_PropertyChangedHandler.remove(token);
    }
} // namespace winrt::Windows::Xbox::Media::implementation
