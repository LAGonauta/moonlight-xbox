﻿#include "pch.h"
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

// Loads and initializes application assets when the application is loaded.
moonlight_xbox_dxMain::moonlight_xbox_dxMain(const std::shared_ptr<DX::DeviceResources>& deviceResources, StreamPage^ streamPage, MoonlightClient* client, StreamConfiguration^ configuration) :

	m_deviceResources(deviceResources), m_pointerLocationX(0.0f), m_streamPage(streamPage), moonlightClient(client)
{
	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	m_sceneRenderer = std::unique_ptr<VideoRenderer>(new VideoRenderer(m_deviceResources, moonlightClient, configuration));

	m_fpsTextRenderer = std::unique_ptr<LogRenderer>(new LogRenderer(m_deviceResources));

	m_statsTextRenderer = std::unique_ptr<StatsRenderer>(new StatsRenderer(m_deviceResources));
	streamPage->m_progressView->Visibility = Windows::UI::Xaml::Visibility::Visible;

	client->OnStatusUpdate = ([streamPage](int status) {
		const char* msg = LiGetStageName(status);
		streamPage->m_statusText->Text = Utils::StringFromStdString(std::string(msg));
		});

	client->OnCompleted = ([streamPage]() {
		streamPage->m_progressView->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		});

	client->OnFailed = ([streamPage](int status, int error, char* message) {
		streamPage->m_statusText->Text = Utils::StringFromStdString(std::string(message));
		});

	client->SetHDR = ([this](bool v) {
		this->m_sceneRenderer->SetHDR(v);
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
	auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction^ action)
		{
			// Calculate the updated frame and render once per vertical blanking interval.
			while (action->Status == AsyncStatus::Started)
			{
				critical_section::scoped_lock lock(m_criticalSection);
				int t1 = GetTickCount64();
				Update();
				if (Render())
				{
					m_deviceResources->Present();
				}
			}
		});
	m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
	if (m_inputLoopWorker != nullptr && m_inputLoopWorker->Status == AsyncStatus::Started) {
		return;
	}
	auto inputItemHandler = ref new WorkItemHandler([this](IAsyncAction^ action)
		{
			// Calculate the updated frame and render once per vertical blanking interval.
			while (action->Status == AsyncStatus::Started)
			{
				ProcessInput();
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
	if (gamepads->Size == 0)return;
	moonlightClient->SetGamepadCount(gamepads->Size);
	auto state = GetApplicationState();
	//Position
	double multiplier = ((double)state->MouseSensitivity) / ((double)4.0f);
	for (int i = 0; i < gamepads->Size; i++) {
		Windows::Gaming::Input::Gamepad^ gamepad = gamepads->GetAt(i);
		auto reading = gamepad->GetCurrentReading();
		//If this combination is pressed on gamed we should handle some magic things :)
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
			m_streamPage->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
				Windows::UI::Xaml::Controls::Flyout::ShowAttachedFlyout(m_streamPage->m_flyoutButton);
				}));
			insideFlyout = true;
		}
		if (insideFlyout)return;
		//If mouse mode is enabled the gamepad acts as a mouse, instead we pass the raw events to the host
		if (keyboardMode) {
			//B to close
			if (isPressed(reading.Buttons, GamepadButtons::B) && !isPressed(previousReading[i].Buttons, GamepadButtons::B)) {
				if (GetApplicationState()->EnableKeyboard) {
					m_streamPage->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
						m_streamPage->m_keyboardView->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
						}));
					keyboardMode = false;
				}
				else {
					CoreInputView::GetForCurrentView()->TryHide();
				}
			}
			//X to backspace
			if (isPressed(reading.Buttons, GamepadButtons::X) && !isPressed(previousReading[i].Buttons, GamepadButtons::X)) {
				moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Back, 0);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::X)) {
				moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Back, 0);
			}
			//Y to Space
			if (isPressed(reading.Buttons, GamepadButtons::Y) && !isPressed(previousReading[i].Buttons, GamepadButtons::Y)) {
				moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Space, 0);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::Y)) {
				moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Space, 0);
			}
			//LB to Left
			if (isPressed(reading.Buttons, GamepadButtons::LeftShoulder) && !isPressed(previousReading[i].Buttons, GamepadButtons::LeftShoulder)) {
				moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Left, 0);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::LeftShoulder)) {
				moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Left, 0);
			}
			//RB to Right
			if (isPressed(reading.Buttons, GamepadButtons::RightShoulder) && !isPressed(previousReading[i].Buttons, GamepadButtons::RightShoulder)) {
				moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Right, 0);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::RightShoulder)) {
				moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Right, 0);
			}
			//Start to Enter
			if (isPressed(reading.Buttons, GamepadButtons::Menu) && !isPressed(previousReading[i].Buttons, GamepadButtons::Menu)) {
				moonlightClient->KeyDown((unsigned short)Windows::System::VirtualKey::Enter, 0);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::Menu)) {
				moonlightClient->KeyUp((unsigned short)Windows::System::VirtualKey::Right, 0);
			}
			//Move with right stick
			if (isPressed(reading.Buttons, GamepadButtons::LeftThumbstick)) {
				moonlightClient->SendScroll(pow(reading.RightThumbstickY * multiplier * 2, 3));
				moonlightClient->SendScrollH(pow(reading.RightThumbstickX * multiplier * 2, 3));
			}
			else {
				//Move with right stick instead of the left one in KB mode
				double x = reading.RightThumbstickX;
				if (abs(x) < 0.1) x = 0;
				else x = x + (x > 0 ? 1 : -1); //Add 1 to make sure < 0 values do not make everything broken
				double y = reading.RightThumbstickY;
				if (abs(y) < 0.1) y = 0;
				else y = (y * -1) + (y > 0 ? -1 : 1); //Add 1 to make sure < 0 values do not make everything broken
				moonlightClient->SendMousePosition(pow(x * multiplier, 3), pow(y * multiplier, 3));
			}
			if (reading.LeftTrigger > 0.25 && previousReading[i].LeftTrigger < 0.25) {
				moonlightClient->SendMousePressed(BUTTON_LEFT);
			}
			else if (reading.LeftTrigger < 0.25 && previousReading[i].LeftTrigger > 0.25) {
				moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			if (reading.RightTrigger > 0.25 && previousReading[i].RightTrigger < 0.25) {
				moonlightClient->SendMousePressed(BUTTON_RIGHT);
			}
			else if (reading.RightTrigger < 0.25 && previousReading[i].RightTrigger > 0.25) {
				moonlightClient->SendMouseReleased(BUTTON_RIGHT);
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
			moonlightClient->SendMousePosition(pow(x * multiplier, 3), pow(y * multiplier, 3));
			//Left Click
			if (isPressed(reading.Buttons, GamepadButtons::A) && !isPressed(previousReading[i].Buttons, GamepadButtons::A)) {
				moonlightClient->SendMousePressed(BUTTON_LEFT);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::A)) {
				moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			//Right Click
			if (isPressed(reading.Buttons, GamepadButtons::X) && !isPressed(previousReading[i].Buttons, GamepadButtons::X)) {
				moonlightClient->SendMousePressed(BUTTON_RIGHT);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::X)) {
				moonlightClient->SendMouseReleased(BUTTON_RIGHT);
			}
			//Left Trigger Click
			if (reading.LeftTrigger > 0.25 && previousReading[i].LeftTrigger < 0.25) {
				moonlightClient->SendMousePressed(BUTTON_LEFT);
			}
			else if (reading.LeftTrigger < 0.25 && previousReading[i].LeftTrigger > 0.25) {
				moonlightClient->SendMouseReleased(BUTTON_LEFT);
			}
			//Right Trigger Click
			if (reading.RightTrigger > 0.25 && previousReading[i].RightTrigger < 0.25) {
				moonlightClient->SendMousePressed(BUTTON_RIGHT);
			}
			else if (reading.RightTrigger < 0.25 && previousReading[i].RightTrigger > 0.25) {
				moonlightClient->SendMouseReleased(BUTTON_RIGHT);
			}
			//Keyboard
			if (!isPressed(reading.Buttons, GamepadButtons::Y) && isPressed(previousReading[i].Buttons, GamepadButtons::Y)) {
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
			moonlightClient->SendScroll(pow(reading.RightThumbstickY * multiplier * 2, 3));
			moonlightClient->SendScrollH(pow(reading.RightThumbstickX * multiplier * 2, 3));
			//Xbox/Guide Button
			//Right Click
			if (isPressed(reading.Buttons, GamepadButtons::B) && !isPressed(previousReading[i].Buttons, GamepadButtons::B)) {
				moonlightClient->SendGuide(i, true);
			}
			else if (isPressed(previousReading[i].Buttons, GamepadButtons::B)) {
				moonlightClient->SendGuide(i, false);
			}
		}
		else {
			moonlightClient->SendGamepadReading(i, reading);
		}
		previousReading[i] = reading;
		usleep(2000);
	}
}


// Renders the current frame according to the current application state.
// Returns true if the frame was rendered and is ready to be displayed.
bool moonlight_xbox_dxMain::Render()
{
	// Don't try to render anything before the first Update.
	if (!m_sceneRenderer->initComplete)
	{
		return false;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	// Reset render targets to the screen.
	ID3D11RenderTargetView* const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };

	// Clear the back buffer and depth stencil view.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::Black);
	context->OMSetRenderTargets(1, targets, nullptr);

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
	moonlightClient->StopStreaming();
	m_sceneRenderer->Stop();
}

void moonlight_xbox_dxMain::CloseApp() {
	moonlightClient->StopApp();
}


void moonlight_xbox_dxMain::OnKeyDown(unsigned short virtualKey, char modifiers)
{
	if (this == nullptr || moonlightClient == nullptr)return;
	moonlightClient->KeyDown(virtualKey, modifiers);
}


void moonlight_xbox_dxMain::OnKeyUp(unsigned short virtualKey, char modifiers)
{
	if (this == nullptr || moonlightClient == nullptr)return;
	moonlightClient->KeyUp(virtualKey, modifiers);
}

void moonlight_xbox_dxMain::ToggleRecording() {
	Utils::Log("Started Recording!\n");
	Windows::Storage::Pickers::FileSavePicker ^picker = ref new Windows::Storage::Pickers::FileSavePicker();
	picker->DefaultFileExtension = ".mp4";
	picker->SuggestedFileName = "recording.mp4";
	auto ext = ref new Platform::Collections::Vector<Platform::String^>();
	ext->Append(".mp4");
	picker->FileTypeChoices->Insert("Video", ext);

	create_task(picker->PickSaveFileAsync()).then([this](Windows::Storage::StorageFile^ file)
	{
			Utils::Log(Utils::PlatformStringToStdString(file->Path).c_str());
	});
}
