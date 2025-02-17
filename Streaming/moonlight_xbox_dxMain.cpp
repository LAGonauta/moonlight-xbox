#include "pch.h"
#include "moonlight_xbox_dxMain.h"
#include "Common\DirectXHelper.h"
#include "Utils.hpp"
#include <Pages/StreamPage.xaml.h>
using namespace Windows::Gaming::Input;


using namespace moonlight_xbox_dx;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Concurrency;
using namespace Windows::UI::ViewManagement::Core;

extern "C" {
#include<Limelight.h>
}

void moonlight_xbox_dx::usleep(unsigned int usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * (__int64)usec);

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (timer == 0)return;
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}

std::shared_ptr<moonlight_xbox_dxMain> moonlight_xbox_dxMain::New(std::shared_ptr<DX::DeviceResources> deviceResources, StreamPage^ streamPage, StreamConfiguration^ configuration)
{
	return std::make_shared<moonlight_xbox_dxMain>(deviceResources, streamPage, configuration);
}

// Loads and initializes application assets when the application is loaded.
moonlight_xbox_dxMain::moonlight_xbox_dxMain(std::shared_ptr<DX::DeviceResources> deviceResources, StreamPage^ streamPage, StreamConfiguration^ configuration) :

	m_deviceResources(deviceResources), m_pointerLocationX(0.0f), m_streamPage(streamPage), m_moonlightClient(std::make_shared<MoonlightClient>())
{
	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	m_sceneRenderer = std::make_unique<VideoRenderer>(m_deviceResources, m_moonlightClient, configuration);

	m_fpsTextRenderer = std::make_unique<LogRenderer>(m_deviceResources);

	m_statsTextRenderer = std::make_unique<StatsRenderer>(m_deviceResources);
	streamPage->m_progressView->Visibility = Windows::UI::Xaml::Visibility::Visible;

	m_moonlightClient->OnStatusUpdate = ([streamPage](int status) {
		const char* msg = LiGetStageName(status);
		streamPage->m_statusText->Text = Utils::StringFromStdString(std::string(msg));
	});

	m_moonlightClient->OnCompleted = ([streamPage]() {
		streamPage->m_progressView->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	});

	m_moonlightClient->OnFailed = ([streamPage](int status, int error, char* message) {
		streamPage->m_statusText->Text = Utils::StringFromStdString(std::string(message));
	});

	m_moonlightClient->SetHDR = ([selfWk = this->weak_from_this()](bool v) {
		if (auto self = selfWk.lock()) {
			self->m_sceneRenderer->SetHDR(v);
		}
	});

	m_timer.SetFixedTimeStep(false);
}

moonlight_xbox_dxMain::~moonlight_xbox_dxMain()
{
	// Deregister device notification
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

// Updates application state when the window size changes (e.g. device orientation change)
void moonlight_xbox_dxMain::CreateWindowSizeDependentResources()
{
	// TODO: Replace this with the size-dependent initialization of your app's content.
	m_sceneRenderer->CreateWindowSizeDependentResources();
}

void moonlight_xbox_dxMain::StartRenderLoop()
{
	// If the animation render loop is already running then do not start another thread.
	if (m_renderLoopWorker != nullptr && m_renderLoopWorker->Status == AsyncStatus::Started)
	{
		return;
	}

	// Create a task that will be run on a background thread.
	auto workItemHandler = ref new WorkItemHandler([selfWk = this->weak_from_this()](IAsyncAction^ action)
		{
			if (auto self = selfWk.lock()) {
				// Calculate the updated frame and render once per vertical blanking interval.
				while (action->Status == AsyncStatus::Started)
				{
					critical_section::scoped_lock lock(self->m_criticalSection);
					int t1 = GetTickCount64();
					self->Update();
					if (self->Render())
					{
						self->m_deviceResources->Present();
					}
				}
			}
		});
	m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
	if (m_inputLoopWorker != nullptr && m_inputLoopWorker->Status == AsyncStatus::Started) {
		return;
	}
	auto inputItemHandler = ref new WorkItemHandler([selfWk = this->weak_from_this()](IAsyncAction^ action)
		{
			if (auto self = selfWk.lock()) {
				// Calculate the updated frame and render once per vertical blanking interval.
				while (action->Status == AsyncStatus::Started)
				{
					self->ProcessInput();
				}
			}
		});

	// Run task on a dedicated high priority background thread.
	m_inputLoopWorker = ThreadPool::RunAsync(inputItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
}

void moonlight_xbox_dxMain::StopRenderLoop()
{
	m_renderLoopWorker->Cancel();
	m_inputLoopWorker->Cancel();
}

// Updates the application state once per frame.
void moonlight_xbox_dxMain::Update()
{
	// Update scene objects.
	m_timer.Tick([&]()
	{
		m_sceneRenderer->Update(m_timer);
		m_fpsTextRenderer->Update(m_timer);
		m_statsTextRenderer->Update(m_timer);
	});
}

bool isPressed(Windows::Gaming::Input::GamepadButtons b, Windows::Gaming::Input::GamepadButtons x) {
	return (b & x) == x;
}

// Process all input from the user before updating game state
void moonlight_xbox_dxMain::ProcessInput()
{
	auto gamepads = Windows::Gaming::Input::Gamepad::Gamepads;
	if (gamepads->Size == 0) {
		return;
	}
	m_moonlightClient->SetGamepadCount(gamepads->Size);
	if (m_previousReading.size() < gamepads->Size) {
		m_previousReading.resize(gamepads->Size);
	}
	auto state = GetApplicationState();
	//Position
	double multiplier = ((double)state->MouseSensitivity) / ((double)4.0f);
	for (int i = 0; i < gamepads->Size; i++) {
		Windows::Gaming::Input::Gamepad^ gamepad = gamepads->GetAt(i);
		auto reading = gamepad->GetCurrentReading();
		//If this combination is pressed on game we should handle some magic things :)
		bool alternateCombination = GetApplicationState()->AlternateCombination;
		bool isCurrentlyPressed = true;
		GamepadButtons magicKey[] = { GamepadButtons::Menu,GamepadButtons::View };
		if (alternateCombination) {
			magicKey[0] = GamepadButtons::LeftShoulder;
			magicKey[1] = GamepadButtons::RightShoulder;
			if (reading.LeftTrigger < 0.25 || reading.RightTrigger < 0.25)isCurrentlyPressed = false;
		}
		for (auto k : magicKey) {
			if ((reading.Buttons & k) != k) {
				isCurrentlyPressed = false;
				break;
			}
		}
		if (isCurrentlyPressed) {
			m_streamPage->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([streamPage = m_streamPage]() {
				Windows::UI::Xaml::Controls::Flyout::ShowAttachedFlyout(streamPage->m_flyoutButton);
			}));
			insideFlyout = true;
		}
		if (insideFlyout)return;
		//If mouse mode is enabled the gamepad acts as a mouse, instead we pass the raw events to the host
		if (keyboardMode) {
			//B to close
			if (isPressed(reading.Buttons, GamepadButtons::B) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::B)) {
				if (GetApplicationState()->EnableKeyboard) {
					m_streamPage->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([streamPage = m_streamPage]() {
						streamPage->m_keyboardView->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
					}));
					keyboardMode = false;
				}
				else {
					CoreInputView::GetForCurrentView()->TryHide();
				}
			}
			//X to backspace
			if (isPressed(reading.Buttons, GamepadButtons::X) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::X)) {
				m_moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Back, 0);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::X)) {
				m_moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Back, 0);
			}
			//Y to Space
			if (isPressed(reading.Buttons, GamepadButtons::Y) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::Y)) {
				m_moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Space, 0);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::Y)) {
				m_moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Space, 0);
			}
			//LB to Left
			if (isPressed(reading.Buttons, GamepadButtons::LeftShoulder) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::LeftShoulder)) {
				m_moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Left, 0);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::LeftShoulder)) {
				m_moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Left, 0);
			}
			//RB to Right
			if (isPressed(reading.Buttons, GamepadButtons::RightShoulder) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::RightShoulder)) {
				m_moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Right, 0);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::RightShoulder)) {
				m_moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Right, 0);
			}
			//Start to Enter
			if (isPressed(reading.Buttons, GamepadButtons::Menu) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::Menu)) {
				m_moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Enter, 0);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::Menu)) {
				m_moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Right, 0);
			}
			//Move with right stick
			if (isPressed(reading.Buttons, GamepadButtons::LeftThumbstick)) {
				m_moonlightClient->SendScroll(pow(reading.RightThumbstickY * multiplier * 2, 3));
				m_moonlightClient->SendScrollH(pow(reading.RightThumbstickX * multiplier * 2, 3));
			}
			else {
				//Move with right stick instead of the left one in KB mode
				double x = reading.RightThumbstickX;
				if (abs(x) < 0.1) x = 0;
				else x = x + (x > 0 ? 1 : -1); //Add 1 to make sure < 0 values do not make everything broken
				double y = reading.RightThumbstickY;
				if (abs(y) < 0.1) y = 0;
				else y = (y * -1) + (y > 0 ? -1 : 1); //Add 1 to make sure < 0 values do not make everything broken
				m_moonlightClient->SendMousePosition(pow(x * multiplier, 3), pow(y * multiplier, 3));
			}
			if (reading.LeftTrigger > 0.25 && m_previousReading[i].LeftTrigger < 0.25) {
				m_moonlightClient->SendMousePressed(BUTTON_LEFT);
			}
			else if (reading.LeftTrigger < 0.25 && m_previousReading[i].LeftTrigger > 0.25) {
				m_moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			if (reading.RightTrigger > 0.25 && m_previousReading[i].RightTrigger < 0.25) {
				m_moonlightClient->SendMousePressed(BUTTON_RIGHT);
			}
			else if (reading.RightTrigger < 0.25 && m_previousReading[i].RightTrigger > 0.25) {
				m_moonlightClient->SendMouseReleased(BUTTON_RIGHT);
			}
		}
		else if (mouseMode) {
			auto state = GetApplicationState();
			//Position
			double multiplier = ((double)state->MouseSensitivity) / ((double)4.0f);
			double x = reading.LeftThumbstickX;
			if (abs(x) < 0.1) x = 0;
			else x = x + (x > 0 ? 1 : -1); //Add 1 to make sure < 0 values do not make everything broken
			double y = reading.LeftThumbstickY;
			if (abs(y) < 0.1) y = 0;
			else y = (y * -1) + (y > 0 ? -1 : 1); //Add 1 to make sure < 0 values do not make everything broken
			m_moonlightClient->SendMousePosition(pow(x * multiplier, 3), pow(y * multiplier, 3));
			//Left Click
			if (isPressed(reading.Buttons, GamepadButtons::A) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::A)) {
				m_moonlightClient->SendMousePressed(BUTTON_LEFT);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::A)) {
				m_moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			//Right Click
			if (isPressed(reading.Buttons, GamepadButtons::X) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::X)) {
				m_moonlightClient->SendMousePressed(BUTTON_RIGHT);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::X)) {
				m_moonlightClient->SendMouseReleased(BUTTON_RIGHT);
			}
			//Left Trigger Click
			if (reading.LeftTrigger > 0.25 && m_previousReading[i].LeftTrigger < 0.25) {
				m_moonlightClient->SendMousePressed(BUTTON_LEFT);
			}
			else if (reading.LeftTrigger < 0.25 && m_previousReading[i].LeftTrigger > 0.25) {
				m_moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			//Right Trigger Click
			if (reading.RightTrigger > 0.25 && m_previousReading[i].RightTrigger < 0.25) {
				m_moonlightClient->SendMousePressed(BUTTON_RIGHT);
			}
			else if (reading.RightTrigger < 0.25 && m_previousReading[i].RightTrigger > 0.25) {
				m_moonlightClient->SendMouseReleased(BUTTON_RIGHT);
			}
			//Keyboard
			if (!isPressed(reading.Buttons, GamepadButtons::Y) && isPressed(m_previousReading[i].Buttons, GamepadButtons::Y)) {
				if (GetApplicationState()->EnableKeyboard) {
					m_streamPage->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
						m_streamPage->m_keyboardView->Visibility = Windows::UI::Xaml::Visibility::Visible;
					}));
					keyboardMode = true;
				}
				else {
					CoreInputView::GetForCurrentView()->TryShow(CoreInputViewKind::Keyboard);
				}
			}
			//Scroll
			m_moonlightClient->SendScroll(pow(reading.RightThumbstickY * multiplier * 2, 3));
			m_moonlightClient->SendScrollH(pow(reading.RightThumbstickX * multiplier * 2, 3));
			//Xbox/Guide Button
			//Right Click
			if (isPressed(reading.Buttons, GamepadButtons::B) && !isPressed(m_previousReading[i].Buttons, GamepadButtons::B)) {
				m_moonlightClient->SendGuide(i, true);
			}
			else if (isPressed(m_previousReading[i].Buttons, GamepadButtons::B)) {
				m_moonlightClient->SendGuide(i, false);
			}
		}
		else {
			m_moonlightClient->SendGamepadReading(i, reading);
		}
		m_previousReading[i] = reading;
		usleep(2000);
	}
}


// Renders the current frame according to the current application state.
// Returns true if the frame was rendered and is ready to be displayed.
bool moonlight_xbox_dxMain::Render()
{
	// Don't try to render anything before the first Update.
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	// Reset render targets to the screen.
	ID3D11RenderTargetView* const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());

	// Clear the back buffer and depth stencil view.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::Black);
	context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	bool shouldPresent = m_sceneRenderer->Render();
	// Render the scene objects.
	m_fpsTextRenderer->Render();
	m_statsTextRenderer->Render();

	return shouldPresent;
}

// Notifies renderers that device resources need to be released.
void moonlight_xbox_dxMain::OnDeviceLost()
{
	m_sceneRenderer->ReleaseDeviceDependentResources();
	m_fpsTextRenderer->ReleaseDeviceDependentResources();
	m_statsTextRenderer->ReleaseDeviceDependentResources();
}

// Notifies renderers that device resources may now be recreated.
void moonlight_xbox_dxMain::OnDeviceRestored()
{
	m_sceneRenderer->CreateDeviceDependentResources();
	m_fpsTextRenderer->CreateDeviceDependentResources();
	m_statsTextRenderer->CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}

void moonlight_xbox_dxMain::SetFlyoutOpened(bool value) {
	insideFlyout = value;
}

void moonlight_xbox_dxMain::Disconnect() {
	m_moonlightClient->StopStreaming();
	m_sceneRenderer->Stop();
}

void moonlight_xbox_dxMain::CloseApp() {
	m_moonlightClient->StopApp();
}

void moonlight_xbox_dxMain::OnKeyDown(unsigned short virtualKey, char modifiers)
{
	if (this == nullptr) {
		return;
	}
	m_moonlightClient->KeyDown(virtualKey, modifiers);
}

void moonlight_xbox_dxMain::OnKeyUp(unsigned short virtualKey, char modifiers)
{
	if (this == nullptr) {
		return;
	}
	m_moonlightClient->KeyUp(virtualKey, modifiers);
}

void moonlight_xbox_dxMain::SendGuideButton(int duration) {
	concurrency::create_async([duration, moonlightClient = m_moonlightClient]() {
		moonlightClient->SendGuide(0, true);
		Sleep(duration);
		moonlightClient->SendGuide(0, false);
	});
}

