#pragma once
#include <Windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Xbox.Media.h>
#include "Windows.Xbox.Media.GameTransportControls.g.h"

namespace winrt::Windows::Xbox::Media::implementation
{
    struct GameTransportControls : GameTransportControlsT<GameTransportControls>
    {
        GameTransportControls() = default;

        hstring Title();
        void Title(hstring const &value);
        hstring Subtitle();
        void Subtitle(hstring const &value);
        winrt::Windows::Xbox::Media::GamePlaybackStatus PlaybackStatus();
        void PlaybackStatus(winrt::Windows::Xbox::Media::GamePlaybackStatus const &value);
        winrt::Windows::Xbox::Media::SoundLevel SoundLevel();
        bool IsEnabled();
        void IsEnabled(bool value);
        bool IsPlayEnabled();
        void IsPlayEnabled(bool value);
        bool IsPauseEnabled();
        void IsPauseEnabled(bool value);
        bool IsMenuEnabled();
        void IsMenuEnabled(bool value);
        bool IsViewEnabled();
        void IsViewEnabled(bool value);
        bool IsBackEnabled();
        void IsBackEnabled(bool value);
        winrt::event_token add_ButtonPressed(
            winrt::Windows::Foundation::TypedEventHandler<
                winrt::Windows::Xbox::Media::GameTransportControls,
                winrt::Windows::Xbox::Media::GameTransportControlsButtonPressedEventArgs> const &handler);
        void remove_ButtonPressed(winrt::event_token const &token);
        winrt::event_token add_PropertyChanged(
            winrt::Windows::Foundation::TypedEventHandler<
                winrt::Windows::Xbox::Media::GameTransportControls,
                winrt::Windows::Xbox::Media::GameTransportControlsPropertyChangedEventArgs> const &handler);
        void remove_PropertyChanged(winrt::event_token const &token);

      private:
        hstring _title = L"";
        hstring _subtitle = L"";
        winrt::Windows::Xbox::Media::GamePlaybackStatus _playbackStatus = winrt::Windows::Xbox::Media::GamePlaybackStatus::Closed;
        bool _isEnabled = true;
        bool _isPlayEnabled = true;
        bool _isPauseEnabled = true;
        bool _isMenuEnabled = true;
        bool _isViewEnabled = true;
        bool _isBackEnabled = true;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<
            winrt::Windows::Xbox::Media::GameTransportControls,
            winrt::Windows::Xbox::Media::GameTransportControlsButtonPressedEventArgs>>
            e_ButtonPressedHandler;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<
            winrt::Windows::Xbox::Media::GameTransportControls,
            winrt::Windows::Xbox::Media::GameTransportControlsPropertyChangedEventArgs>>
            e_PropertyChangedHandler;
    };
}

namespace winrt::Windows::Xbox::Media::factory_implementation
{
	struct GameTransportControls : GameTransportControlsT<GameTransportControls, implementation::GameTransportControls>
	{
	};
}