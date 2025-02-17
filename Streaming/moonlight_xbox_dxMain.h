#pragma once

#include <vector>
#include <array>
#include <memory>

#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Streaming\VideoRenderer.h"
#include "Streaming\LogRenderer.h"
#include "Streaming\StatsRenderer.h"
#include "Pages\StreamPage.xaml.h"

// Renders Direct2D and 3D content on the screen.
namespace moonlight_xbox_dx
{
	class moonlight_xbox_dxMain : public DX::IDeviceNotify, public std::enable_shared_from_this<moonlight_xbox_dxMain>
	{
	public:
		static std::shared_ptr<moonlight_xbox_dxMain> New(std::shared_ptr<DX::DeviceResources> deviceResources, StreamPage^ streamPage, StreamConfiguration^ configuration);
		moonlight_xbox_dxMain(std::shared_ptr<DX::DeviceResources> deviceResources, StreamPage^ streamPage, StreamConfiguration^ configuration);
		~moonlight_xbox_dxMain();
		void CreateWindowSizeDependentResources();
		void TrackingUpdate(float positionX) { m_pointerLocationX = positionX; }
		void StartRenderLoop();
		void StopRenderLoop();
		void SetFlyoutOpened(bool value);
		Concurrency::critical_section& GetCriticalSection() { return m_criticalSection; }
		bool mouseMode = false;
		bool keyboardMode = false;
		void OnKeyDown(unsigned short virtualKey, char modifiers);
		void OnKeyUp(unsigned short virtualKey, char modifiers);
		// IDeviceNotify
		virtual void OnDeviceLost();
		virtual void OnDeviceRestored();
		void Disconnect();
		void CloseApp();
		void SendGuideButton(int duration);
	private:
		void ProcessInput();
		void Update();
		bool Render();

		// We only want this class to be used through the shared pointer
		moonlight_xbox_dxMain(const moonlight_xbox_dxMain&) = delete;
		moonlight_xbox_dxMain& operator=(const moonlight_xbox_dxMain&) = delete;
		moonlight_xbox_dxMain(moonlight_xbox_dxMain&&) = delete;
		moonlight_xbox_dxMain& operator=(moonlight_xbox_dxMain&&) = delete;

		// Cached pointer to device resources.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;

		// TODO: Replace with your own content renderers.
		// TODO: Replace with your own content renderers.
		std::unique_ptr<VideoRenderer> m_sceneRenderer;
		std::unique_ptr<LogRenderer> m_fpsTextRenderer;
		std::unique_ptr<StatsRenderer> m_statsTextRenderer;

		Windows::Foundation::IAsyncAction^ m_renderLoopWorker;
		Windows::Foundation::IAsyncAction^ m_inputLoopWorker;
		Concurrency::critical_section m_criticalSection;

		// Rendering loop timer.
		DX::StepTimer m_timer;

		// Track current input pointer position.
		float m_pointerLocationX;
		bool insideFlyout = false;
		std::vector<Windows::Gaming::Input::GamepadReading> m_previousReading;
		StreamPage^ m_streamPage;
		std::shared_ptr<MoonlightClient> m_moonlightClient;
	};
	void usleep(unsigned int usec);
}