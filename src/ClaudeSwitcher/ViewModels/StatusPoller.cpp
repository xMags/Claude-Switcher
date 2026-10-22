#include "pch.h"
#include "ViewModels/StatusPoller.h"

#include <chrono>
#include <utility>

namespace ClaudeSwitcher::ViewModels
{
    namespace
    {
        constexpr std::chrono::seconds PollInterval{ 2 };
    }

    StatusPoller::StatusPoller(
        winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
        std::wstring packageFamilyName,
        ResultHandler handler)
        : m_queue(std::move(queue)),
          m_packageFamilyName(std::move(packageFamilyName)),
          m_state(std::make_shared<State>())
    {
        m_state->Handler = std::move(handler);
        m_timer = m_queue.CreateTimer();
        m_timer.Interval(PollInterval);
        m_timer.IsRepeating(true);
        // The timer belongs to this object and is stopped in the destructor,
        // so the raw this capture cannot outlive it.
        m_tickToken = m_timer.Tick([this](auto&&, auto&&) { RefreshNow(); });
    }

    StatusPoller::~StatusPoller()
    {
        m_timer.Stop();
        m_timer.Tick(m_tickToken);
        // In-flight scans hold only a weak reference; releasing the state
        // makes them finish without touching anything.
        m_state.reset();
    }

    void StatusPoller::Start()
    {
        if (!m_timer.IsRunning())
        {
            m_timer.Start();
            RefreshNow();
        }
    }

    void StatusPoller::Stop()
    {
        m_timer.Stop();
        // Results of scans already running are no longer wanted.
        ++m_state->Generation;
    }

    void StatusPoller::RefreshNow()
    {
        auto const generation = ++m_state->Generation;
        Scan(m_state, generation, m_queue, m_packageFamilyName);
    }

    winrt::fire_and_forget StatusPoller::Scan(
        std::weak_ptr<State> weakState,
        std::uint64_t generation,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
        std::wstring packageFamilyName)
    {
        co_await winrt::resume_background();

        std::vector<::ClaudeSwitcher::Core::ClaudeInstance> instances;
        try
        {
            instances = ::ClaudeSwitcher::Core::ScanClaudeInstances(packageFamilyName);
        }
        catch (...)
        {
            // A failed scan keeps the previous state on screen; the next tick retries.
            co_return;
        }

        try
        {
            co_await wil::resume_foreground(queue);
        }
        catch (...)
        {
            // The queue is shutting down with the window.
            co_return;
        }

        auto const state = weakState.lock();
        if (!state || state->Generation != generation || !state->Handler)
        {
            co_return;
        }
        try
        {
            state->Handler(std::move(instances));
        }
        catch (...)
        {
            // A fire_and_forget coroutine must not let an exception escape.
        }
    }
}
