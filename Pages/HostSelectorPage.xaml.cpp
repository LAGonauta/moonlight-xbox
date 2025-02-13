//
// HostSelectorPage.xaml.cpp
// Implementation of the HostSelectorPage class
//

#include "pch.h"
#include <sstream>
#include <array>

#include "HostSelectorPage.xaml.h"
#include "AppPage.xaml.h"
#include <State\MoonlightClient.h>
#include "HostSettingsPage.xaml.h"
#include "Utils.hpp"
#include "MoonlightSettings.xaml.h"
#include "State\MDNSHandler.h"
#include "MoonlightWelcome.xaml.h"
#include "Utils.hpp"

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::UI::ViewManagement::Core;


// The Blank Page item template is documented at https://go.microsoft.com/fwlink/?LinkId=234238

HostSelectorPage::HostSelectorPage()
{
	state = GetApplicationState();
	InitializeComponent();
}

void HostSelectorPage::NewHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	dialogHostnameTextBox = ref new TextBox();
	dialogHostnameTextBox->AcceptsReturn = false;
	dialogHostnameTextBox->KeyDown += ref new Windows::UI::Xaml::Input::KeyEventHandler(this, &HostSelectorPage::OnKeyDown);
	ContentDialog^ dialog = ref new ContentDialog();
	dialog->Content = dialogHostnameTextBox;
	dialog->Title = L"Add new Host";
	dialog->IsSecondaryButtonEnabled = true;
	dialog->PrimaryButtonText = "Ok";
	dialog->SecondaryButtonText = "Cancel";
	dialog->ShowAsync();
	dialog->PrimaryButtonClick += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Xaml::Controls::ContentDialog^, Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs^>(this, &HostSelectorPage::OnNewHostDialogPrimaryClick);
}

void HostSelectorPage::OnNewHostDialogPrimaryClick(Windows::UI::Xaml::Controls::ContentDialog^ sender, Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs^ args)
{
	sender->IsPrimaryButtonEnabled = false;
	Platform::String^ hostname = dialogHostnameTextBox->Text;
	auto def = args->GetDeferral();
	Concurrency::create_task([def, hostname, this, args, sender]() {
		bool status = state->AddHost(hostname);
		if (!status) {
			Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([sender, hostname, def, args]() {
				args->Cancel = true;
				sender->Content = L"Failed to Connect to " + hostname;
				def->Complete();
				}));
			return;
		}
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([def]() {
			def->Complete();
			}));
		});
}

void HostSelectorPage::GridView_ItemClick(Platform::Object^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e)
{
	MoonlightHost^ host = (MoonlightHost^)e->ClickedItem;
	this->Connect(host);
}

void HostSelectorPage::StartPairing(MoonlightHost^ host) {
	auto client = std::make_shared<MoonlightClient>();
	int status = client->Connect(Utils::WideToNarrowString(host->LastHostname->Data()));
	if (status != 0) {
		return;
	}
	auto pin = client->GeneratePIN();
	ContentDialog^ dialog = ref new ContentDialog();
	std::wstringstream msg;
	msg << L"We need to pair the host before continuing. Type " << pin << L" on your host to continue";
	dialog->Content = ref new Platform::String(msg.str().c_str());
	dialog->PrimaryButtonText = "Ok";
	dialog->ShowAsync();
	Concurrency::create_task([dialog, host, client]() {
		auto err = client->Pair();
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([err, dialog, host, client]()
			{
				if (err == 0) {
					dialog->Hide();
				}
				else {
					//dialog->Content = Utils::StringFromStdString(std::string(gs_error));
				}
				host->UpdateStats();
			}
		));
	});
}

void HostSelectorPage::removeHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	State->RemoveHost(currentHost);
}

void HostSelectorPage::HostsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e)
{
	//Thanks to https://stackoverflow.com/questions/62878368/uwp-gridview-listview-get-the-righttapped-item-supporting-both-mouse-and-xbox-co
	FrameworkElement^ senderElement = (FrameworkElement^)e->OriginalSource;

	if (senderElement->GetType()->FullName->Equals(GridViewItem::typeid->FullName)) {
		auto gi = (GridViewItem^)senderElement;
		currentHost = (MoonlightHost^)(gi->Content);
	}
	else {
		currentHost = (MoonlightHost^)(senderElement->DataContext);
	}
	if (currentHost == nullptr) {
		return;
	}
	this->ActionsFlyout->ShowAt(senderElement);
}

void HostSelectorPage::hostSettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), currentHost);
}

void HostSelectorPage::SettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid));
}

void HostSelectorPage::OnStateLoaded() {
	if (GetApplicationState()->FirstTime) {
		this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid));
		return;
	}
	Concurrency::create_task([]() {
		for (auto a : GetApplicationState()->SavedHosts) {
			a->UpdateStats();
		}
		}).then([this]() {
			if (GetApplicationState()->autostartInstance.size() > 0) {
				auto pii = Utils::StringFromStdString(GetApplicationState()->autostartInstance);
				for (unsigned int i = 0; i < GetApplicationState()->SavedHosts->Size; i++) {
					auto host = GetApplicationState()->SavedHosts->GetAt(i);
					if (host->InstanceId->Equals(pii)) {
						auto that = this;
						Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([that, host]() {
							that->Connect(host);
							}));
						break;
					}
				}
			}
			});

}

void HostSelectorPage::Connect(MoonlightHost^ host) {
	if (!host->Connected)return;
	if (!host->Paired) {
		StartPairing(host);
		return;
	}
	state->shouldAutoConnect = true;
	continueFetch = false;
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(AppPage::typeid), host);
}

void HostSelectorPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseVisible);
	continueFetch = true;
	Concurrency::create_task([this] {
		init_mdns();
		while (continueFetch) {
			query_mdns();
			for (auto a : GetApplicationState()->SavedHosts) {
				a->UpdateStats();
			}
			Sleep(5000);
		}
		});
}

void HostSelectorPage::OnKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e)
{
	if (e->Key == Windows::System::VirtualKey::Enter) {
		CoreInputView::GetForCurrentView()->TryHide();
	}
}

void moonlight_xbox_dx::HostSelectorPage::wakeHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (currentHost == nullptr) {
		return;
	}

	try {
		bool success = State->WakeHost(currentHost);

		ContentDialog^ dialog = ref new ContentDialog();
		dialog->Title = success ? "Wake Host" : "Wake Host Failed";
		dialog->Content = success
			? "Wake-on-LAN packet sent successfully to " + currentHost->ComputerName
			: "Failed to send Wake-on-LAN packet.\n\nPlease check if Wake-on-LAN is enabled on the host.";
		dialog->PrimaryButtonText = "OK";
		dialog->ShowAsync();
	}
	catch (std::exception ex) {
		ContentDialog^ dialog = ref new ContentDialog();
		dialog->Title = "Wake Host Error";
		dialog->Content = "An error occurred while trying to wake the host:\n\n" + Utils::StringFromChars((char*)ex.what());
		dialog->PrimaryButtonText = "OK";
		dialog->ShowAsync();
	}
}