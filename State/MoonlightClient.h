#pragma once
#include "pch.h"
#include "Common/DeviceResources.h"
#include <State/StreamConfiguration.h>
#include "State\MoonlightApp.h"
extern "C" {
	#include <libavcodec/avcodec.h>
	#include <Limelight.h>
	#include<libgamestream/client.h>
}

typedef void(*MoonlightErrorCallback)(const char *msg);
namespace moonlight_xbox_dx {
	class MoonlightClient
	{
	public:
		MoonlightClient();
		int StartStreaming(std::shared_ptr<DX::DeviceResources> res, StreamConfiguration ^config);
		int Connect(const std::string& hostname);
		bool IsPaired();
		int Pair();
		std::wstring GeneratePIN();
		std::vector<MoonlightApp^> MoonlightClient::GetApplications();
		void SendGamepadReading(short controllerNumber, Windows::Gaming::Input::GamepadReading reading);
		void SendMousePosition(float x, float y);
		void SendMousePressed(int button);
		void SendMouseReleased(int button);
		void SendScroll(float value);
		void SendScrollH(float value);
		void SetSoftwareEncoder(bool value);
		void SetGamepadCount(short count);
		int GetRunningAppID();
		void StopStreaming();
		void StopApp();
		void Unpair();
		void KeyDown(unsigned short v,char modifiers);
		void KeyUp(unsigned short v, char modifiers);
		void SendGuide(int controllerNumber, bool v);
		Platform::String^ GetInstanceID();
		Platform::String^ GetComputerName();
		Platform::String^ GetServerAddress();
		Platform::String^ GetServerMacAddress();
		std::function<void(int)> OnStatusUpdate;
		std::function<void()> OnCompleted;
		std::function<void(bool)> SetHDR;
		std::function<void(int,int, char*)> OnFailed;
	private:
		SERVER_DATA serverData{};
		std::string connectionPin;
		std::string hostname;
		int port = 0;
		bool useSoftwareEncoder = false;
		int activeGamepadMask = 0;
	};
}