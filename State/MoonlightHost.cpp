#include "pch.h"
#include "MoonlightHost.h"
#include "State\MoonlightClient.h"
#include <ppltasks.h>
#include "Utils.hpp"

namespace moonlight_xbox_dx {
	void MoonlightHost::UpdateStats() {
		this->Loading = true;
		bool status = this->Connect() == 0;
		this->Connected = status;
		if (status) {
			this->Paired = client->IsPaired();
			this->CurrentlyRunningAppId = client->GetRunningAppID();
			this->InstanceId = client->GetInstanceID();
			if (this->Connected) {
				this->ComputerName = client->GetComputerName();
				this->ServerAddress = client->GetServerAddress();
				this->MacAddress = client->GetServerMacAddress();
			}
		}
		this->Loading = false;
	}

	int MoonlightHost::Connect()
	{
		client = std::make_shared<MoonlightClient>();
		Platform::String^ ipAddress = this->lastHostname;

		return client->Connect(Utils::PlatformStringToStdString(ipAddress));
	}

	void MoonlightHost::UpdateApps() {
		auto apps = client->GetApplications();
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this, apps]() {
			Apps->Clear();
			for (auto a : apps) {
				if (a->Id == CurrentlyRunningAppId)a->CurrentlyRunning = true;
				Apps->Append(a);
			}
		}));
	}


	void MoonlightHost::Unpair()
	{
		client->Unpair();
	}

	
	void MoonlightHost::OnPropertyChanged(Platform::String^ propertyName)
	{
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this, propertyName]() {
			PropertyChanged(this, ref new  Windows::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
		}));
	}
}