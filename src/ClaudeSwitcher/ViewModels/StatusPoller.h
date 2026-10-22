#pragma once

#include "Core/ProcessScanner.h"

#include <winrt/Microsoft.UI.Dispatching.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ClaudeSwitcher::ViewModels
{
    // Scans for running Claude sessions every couple of seconds on a thread
    // pool thread and hands each result to the UI thread.
    //
    // Threading: construct, call and destroy on the UI thread that owns the
    // dispatcher queue; the handler also runs there. A scan still in flight
    // when a newer one starts, or when the poller is destroyed, is dropped
    // without calling the handler.
    class StatusPoller
    {
    public:
        using ResultHandler = std::function<void(std::vector<::ClaudeSwitcher::Core::ClaudeInstance>)>;

        StatusPoller(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue, std::wstring packageFamilyName, ResultHandler handler);
        ~StatusPoller();

        StatusPoller(StatusPoller const&) = delete;
        StatusPoller& operator=(StatusPoller const&) = delete;

        void Start();
        void Stop();
        void RefreshNow();

    private:
        struct State
        {
            ResultHandler Handler;
            std::uint64_t Generation = 0;
        };

        static winrt::fire_and_forget Scan(
            std::weak_ptr<State> weakState,
            std::uint64_t generation,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
            std::wstring packageFamilyName);

        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue;
        std::wstring m_packageFamilyName;
        std::shared_ptr<State> m_state;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        winrt::event_token m_tickToken{};
    };
}
