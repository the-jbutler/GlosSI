#include "SteamTargetRenderer.h"

ULONG SteamTargetRenderer::ulTargetSerials[XUSER_MAX_COUNT];

SteamTargetRenderer::SteamTargetRenderer()
{
	iRealControllers = getRealControllers();

	getSteamOverlay();

#ifdef NDEBUG
	if (overlayPtr != NULL)
		openUserWindow();
	else
		exit(1);
#else
	openUserWindow();
	bDrawDebugEdges = true;
#endif // NDEBUG

	QSettings settings(".\\TargetConfig.ini", QSettings::IniFormat);
	settings.beginGroup("BaseConf");
	const QStringList childKeys = settings.childKeys();
	for (auto &childkey : childKeys)
	{
		if (childkey == "bDrawDebugEdges")
		{
			bDrawDebugEdges = settings.value(childkey).toBool();
		} else if (childkey == "bShowDebugConsole") {
			bShowDebugConsole = settings.value(childkey).toBool();
		} else if (childkey == "bEnableOverlay") {
			bDrawOverlay = settings.value(childkey).toBool();
		} else if (childkey == "bEnableControllers") {
			bPauseControllers = !settings.value(childkey).toBool();
		} else if (childkey == "bEnableVsync") {
			bVsync = settings.value(childkey).toBool();
		} else if (childkey == "iRefreshRate") {
			iRefreshRate = settings.value(childkey).toInt();
		}
	}
	settings.endGroup();

	sfCshape = sf::CircleShape(100.f);
	sfCshape.setFillColor(sf::Color(128, 128, 128, 128));
	sfCshape.setOrigin(sf::Vector2f(100, 100));
	sf::VideoMode mode = sf::VideoMode::getDesktopMode();
	sfWindow.create(sf::VideoMode(mode.width-16, mode.height-32), "OverlayWindow"); //Window is too large ; always 16 and 32 pixels?  - sf::Style::None breaks transparency!
	sfWindow.setVerticalSyncEnabled(bVsync);
	if (!bVsync)
		sfWindow.setFramerateLimit(iRefreshRate);
	sfWindow.setPosition(sf::Vector2i(0, 0));
	makeSfWindowTransparent(sfWindow);

	sfWindow.setActive(false);
	consoleHwnd = GetConsoleWindow(); //We need a console for a dirty hack to make sure we stay in game bindings - Also useful for debugging

	LONG_PTR style = GetWindowLongPtr(consoleHwnd, GWL_STYLE); 
	SetWindowLongPtr(consoleHwnd, GWL_STYLE, style & ~WS_SYSMENU);

	if(!bShowDebugConsole) {
		ShowWindow(consoleHwnd, SW_HIDE); //Hide the console window; it just confuses the user;
	}

	if (!VIGEM_SUCCESS(vigem_init()))
	{
		std::cout << "Error initializing ViGem!" << std::endl;
		bRunLoop = false;
	}


	VIGEM_TARGET vtX360[XUSER_MAX_COUNT];
	for (int i = 0; i < XUSER_MAX_COUNT; i++)
	{
		VIGEM_TARGET_INIT(&vtX360[i]);
		SteamTargetRenderer::ulTargetSerials[i] = NULL;
	}

	QTimer::singleShot(2000, this, &SteamTargetRenderer::launchApp); // lets steam do its thing

}

SteamTargetRenderer::~SteamTargetRenderer()
{	
	bRunLoop = false;
	renderThread.join();
	for (int i = 0; i < XUSER_MAX_COUNT; i++)
	{
		vigem_target_unplug(&vtX360[i]);

	}
	vigem_shutdown();
	qpUserWindow->kill();
	delete qpUserWindow;
}

void SteamTargetRenderer::run()
{
	renderThread = std::thread(&SteamTargetRenderer::RunSfWindowLoop, this);
}

void SteamTargetRenderer::controllerCallback(VIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber)
{
	std::cout << "Target Serial: " << Target.SerialNo
		<< "; LMotor: " << (unsigned int)(LargeMotor * 0xff) << "; "
		<< " SMotor: " << (unsigned int)(SmallMotor * 0xff) << "; " << std::endl;

	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = LargeMotor * 0xff; 
	vibration.wRightMotorSpeed = SmallMotor * 0xff; 


	for (int i = 0; i < XUSER_MAX_COUNT; i++)
	{
		if (SteamTargetRenderer::ulTargetSerials[i] == Target.SerialNo)
		{
			XInputSetState(i, &vibration);
		}
	}
}

void SteamTargetRenderer::RunSfWindowLoop()
{
	if (!bRunLoop)
		return;
	sfWindow.setActive(true);
	DWORD result;
	bool focusSwitchNeeded = true;
	sf::Clock reCheckControllerTimer;

	if (!bDrawOverlay)
	{
		ShowWindow(consoleHwnd, SW_HIDE);
	}

	while (sfWindow.isOpen() && bRunLoop)
	{
		if (bDrawOverlay)
		{
			SetWindowPos(sfWindow.getSystemHandle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		sf::Event event;
		while (sfWindow.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				sfWindow.close();
		}

		sfWindow.clear(sf::Color::Transparent);

		if (bDrawDebugEdges)
			drawDebugEdges();

		sfWindow.display();

		if (!bPauseControllers)
		{

			if (reCheckControllerTimer.getElapsedTime().asSeconds() >= 1.f)
			{
				int totalbefore = iTotalControllers;
				iTotalControllers = 0;
				for (int i = 0; i < XUSER_MAX_COUNT; i++)
				{
					ZeroMemory(&xsState[i], sizeof(XINPUT_STATE));

					result = XInputGetState(i, &xsState[i]);

					if (result == ERROR_SUCCESS)
					{
						iTotalControllers++;
					}
					else {
						break;
					}
				}
				iTotalControllers -= iVirtualControllers;
				reCheckControllerTimer.restart();
			}

			for (int i = iRealControllers; i < iTotalControllers && i < XUSER_MAX_COUNT; i++)
			{
				////////
				ZeroMemory(&xsState[i], sizeof(XINPUT_STATE));


				result = XInputGetState(i, &xsState[i]);

				if (result == ERROR_SUCCESS)
				{

					if (VIGEM_SUCCESS(vigem_target_plugin(Xbox360Wired, &vtX360[i])))
					{
						iVirtualControllers++;

						std::cout << "Plugged in controller " << vtX360[i].SerialNo << std::endl;

						SteamTargetRenderer::ulTargetSerials[i] = vtX360[i].SerialNo;

						vigem_register_xusb_notification((PVIGEM_XUSB_NOTIFICATION)&SteamTargetRenderer::controllerCallback, vtX360[i]);
					}

					RtlCopyMemory(&xrReport[i], &xsState[i].Gamepad, sizeof(XUSB_REPORT));

					vigem_xusb_submit_report(vtX360[i], xrReport[i]);
				}
				else
				{
					if (VIGEM_SUCCESS(vigem_target_unplug(&vtX360[i])))
					{
						iVirtualControllers--;
						iTotalControllers = 0;
						for (int i = 0; i < XUSER_MAX_COUNT; i++)
						{
							ZeroMemory(&xsState[i], sizeof(XINPUT_STATE));

							result = XInputGetState(i, &xsState[i]);

							if (result == ERROR_SUCCESS)
							{
								iTotalControllers++;
							}
							else {
								break;
							}
						}
						iTotalControllers -= iVirtualControllers;
						std::cout << "Unplugged controller " << vtX360[i].SerialNo << std::endl;
						SteamTargetRenderer::ulTargetSerials[i] = NULL;
					}
				}
			}


			//This ensures that we stay in game binding, even if focused application changes! (Why does this work? Well, i dunno... ask Valve...)
			//Only works with a console window
			//Causes trouble as soon as there is more than the consoleWindow and the overlayWindow
			//This is trying to avoid hooking Steam.exe
			if (focusSwitchNeeded)
			{
				focusSwitchNeeded = false;
				SetFocus(consoleHwnd);
				SetForegroundWindow(consoleHwnd);
			}

			//Dirty hack to make the steamoverlay work properly and still keep Apps Controllerconfig when closing overlay.
			//This is trying to avoid hooking Steam.exe
			if (overlayPtr != NULL)
			{
				char overlayOpen = *(char*)overlayPtr;
				if (overlayOpen)
				{
					if (!bNeedFocusSwitch)
					{
						bNeedFocusSwitch = true;

						hwForeGroundWindow = GetForegroundWindow();

						std::cout << "ForegorundWindow HWND: " << hwForeGroundWindow << std::endl;

						SetFocus(consoleHwnd);
						SetForegroundWindow(consoleHwnd);

						SetFocus(sfWindow.getSystemHandle());
						SetForegroundWindow(sfWindow.getSystemHandle());

						SetWindowLong(sfWindow.getSystemHandle(), GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);

						SetFocus(consoleHwnd);
						SetForegroundWindow(consoleHwnd);
					}
				}
				else {
					if (bNeedFocusSwitch)
					{

						SetFocus(sfWindow.getSystemHandle());
						SetForegroundWindow(sfWindow.getSystemHandle());

						SetWindowLong(sfWindow.getSystemHandle(), GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);

						SetFocus(consoleHwnd);
						SetForegroundWindow(consoleHwnd);

						SetFocus(hwForeGroundWindow);
						SetForegroundWindow(hwForeGroundWindow);

						bNeedFocusSwitch = false;
					}
				}
			}
		}
	}
}

void SteamTargetRenderer::getSteamOverlay()
{
	hmodGameOverlayRenderer = GetModuleHandle(L"Gameoverlayrenderer64.dll");

	if (hmodGameOverlayRenderer != NULL)
	{
		std::cout << "GameOverlayrenderer64.dll found;  Module at: 0x" << hmodGameOverlayRenderer << std::endl;
		overlayPtr = (uint64_t*)(uint64_t(hmodGameOverlayRenderer) + 0x1365e8);
		overlayPtr = (uint64_t*)(*overlayPtr + 0x40);
	}
}

int SteamTargetRenderer::getRealControllers()
{
	int realControllers = 0;
	UINT numDevices = NULL;

	GetRawInputDeviceList(NULL, &numDevices, sizeof(RAWINPUTDEVICELIST));

	PRAWINPUTDEVICELIST rawInputDeviceList;
	rawInputDeviceList = (PRAWINPUTDEVICELIST)malloc(sizeof(RAWINPUTDEVICELIST) * numDevices);
	GetRawInputDeviceList(rawInputDeviceList, &numDevices, sizeof(RAWINPUTDEVICELIST));

	for (unsigned int i = 0; i < numDevices; i++)
	{
		RID_DEVICE_INFO devInfo;
		devInfo.cbSize = sizeof(RID_DEVICE_INFO);
		GetRawInputDeviceInfo(rawInputDeviceList[i].hDevice, RIDI_DEVICEINFO, &devInfo, (PUINT)&devInfo.cbSize);
		if (devInfo.hid.dwVendorId == 0x45e && devInfo.hid.dwProductId == 0x28e)
		{
			realControllers++;
		}

	}

	free(rawInputDeviceList);
	std::cout << "Detected " << realControllers << " real connected X360 Controllers" << std::endl;
	return realControllers;
}


void SteamTargetRenderer::makeSfWindowTransparent(sf::RenderWindow & window)
{
	HWND hwnd = window.getSystemHandle();
	SetWindowLong(hwnd, GWL_STYLE, WS_VISIBLE | WS_POPUP &~WS_CAPTION);
	SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);

	MARGINS margins;
	margins.cxLeftWidth = -1;

	DwmExtendFrameIntoClientArea(hwnd, &margins);
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

	window.clear(sf::Color::Transparent);
	window.display();
}

void SteamTargetRenderer::drawDebugEdges()
{
	sfCshape.setPosition(sf::Vector2f(-25, -25));
	sfWindow.draw(sfCshape);
	sfCshape.setPosition(sf::Vector2f(sfWindow.getSize().x + 25, -25));
	sfWindow.draw(sfCshape);
	sfCshape.setPosition(sf::Vector2f(-25, sfWindow.getSize().y));
	sfWindow.draw(sfCshape);
	sfCshape.setPosition(sf::Vector2f(sfWindow.getSize().x, sfWindow.getSize().y));
	sfWindow.draw(sfCshape);

}

void SteamTargetRenderer::openUserWindow()
{
	qpUserWindow = new QProcess(this);
	qpUserWindow->start("SteamTargetUserWindow.exe", QStringList(), QProcess::ReadWrite);
	qpUserWindow->waitForStarted();
	connect(qpUserWindow, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
		this, &SteamTargetRenderer::userWindowFinished);
	connect(qpUserWindow, SIGNAL(readyRead()) , this,SLOT(readChildProcess()));
}

void SteamTargetRenderer::userWindowFinished()
{
	delete qpUserWindow;
	bRunLoop = false;
	renderThread.join();
	for (int i = 0; i < XUSER_MAX_COUNT; i++)
	{
		vigem_target_unplug(&vtX360[i]);

	}
	vigem_shutdown();
	exit(0);
}

void SteamTargetRenderer::launchApp()
{

	bool launchGame = false;
	QString type = "Win32";
	QString path = "";
	QSettings settings(".\\TargetConfig.ini", QSettings::IniFormat);
	settings.beginGroup("LaunchGame");
	const QStringList childKeys = settings.childKeys();
	for (auto &childkey : childKeys)
	{
		if (childkey == "bLaunchGame")
		{
			launchGame = settings.value(childkey).toBool();
		}
		else if (childkey == "Type") {
			type = settings.value(childkey).toString();
		}
		else if (childkey == "Path") {
			path = settings.value(childkey).toString();
		}
	}
	settings.endGroup();

	if (launchGame)
	{
		QSharedMemory sharedMemInstance("GloSC_GameLauncher");
		if (!sharedMemInstance.create(1024) && sharedMemInstance.error() == QSharedMemory::AlreadyExists)
		{
			QStringList stringList;
			if (type == "Win32")
			{
				stringList << "LaunchWin32Game";
			} else if (type == "UWP") {
				stringList << "LaunchUWPGame";

				/*bool drawOverlayBefore = bDrawOverlay;
				bDrawOverlay = false;
				QTimer::singleShot(5000, [=] {
					bDrawOverlay = drawOverlayBefore;
				});*/

			}
			stringList << path;

			QBuffer buffer;
			buffer.open(QBuffer::ReadWrite);
			QDataStream out(&buffer);
			out << stringList;
			int size = buffer.size();

			sharedMemInstance.attach();
			char *to = (char*)sharedMemInstance.data();
			const char *from = buffer.data().data();
			memcpy(to, from, qMin(sharedMemInstance.size(), size));
			sharedMemInstance.unlock();
			sharedMemInstance.detach();
		}
	}
}

void SteamTargetRenderer::readChildProcess()
{
	QString message(qpUserWindow->readLine());
	if (message.contains("ResetControllers"))
	{
		bPauseControllers = true;
		for (int i = 0; i < XUSER_MAX_COUNT; i++)
		{
			vigem_target_unplug(&vtX360[i]);
			SteamTargetRenderer::ulTargetSerials[i] = NULL;
		}
		Sleep(1000); //give the driver time to unplug before redetecting
		iRealControllers = 0;
		iTotalControllers = 0;
		iVirtualControllers = 0;
		iRealControllers = getRealControllers();
		bPauseControllers = false;
	} else if (message.contains("ShowConsole")) {
		message.chop(1);
		message.remove("ShowConsole ");
		int showConsole = message.toInt();
		if (showConsole > 0)
		{
			bShowDebugConsole = true;
			ShowWindow(consoleHwnd, SW_SHOW);
			SetFocus(consoleHwnd);
			SetForegroundWindow(consoleHwnd);
		} else {
			bShowDebugConsole = false;
			ShowWindow(consoleHwnd, SW_HIDE);
			SetFocus(consoleHwnd);
			SetForegroundWindow(consoleHwnd);
		}
	} else if (message.contains("ShowOverlay")) {
		message.chop(1);
		message.remove("ShowOverlay ");
		int showOverlay = message.toInt();
		if (showOverlay > 0)
		{
			ShowWindow(sfWindow.getSystemHandle(), SW_SHOW);
			SetFocus(consoleHwnd);
			SetForegroundWindow(consoleHwnd);
		} else {
			ShowWindow(sfWindow.getSystemHandle(), SW_HIDE);
			SetFocus(consoleHwnd);
			SetForegroundWindow(consoleHwnd);
		}
	} else if (message.contains("EnableControllers")) {
		message.chop(1);
		message.remove("EnableControllers ");
		int enableControllers = message.toInt();
		if (enableControllers > 0)
		{
			for (int i = 0; i < XUSER_MAX_COUNT; i++)
			{
				vigem_target_unplug(&vtX360[i]);
				SteamTargetRenderer::ulTargetSerials[i] = NULL;
			}
			Sleep(1000); //give the driver time to unplug before redetecting
			iRealControllers = 0;
			iTotalControllers = 0;
			iVirtualControllers = 0;
			iRealControllers = getRealControllers();
			bPauseControllers = false;
		} else {
			bPauseControllers = true;
			for (int i = 0; i < XUSER_MAX_COUNT; i++)
			{
				vigem_target_unplug(&vtX360[i]);
				SteamTargetRenderer::ulTargetSerials[i] = NULL;
			}
			Sleep(1000); //give the driver time to unplug before redetecting
			iRealControllers = 0;
			iVirtualControllers = 0;
			iRealControllers = getRealControllers();
		}
	}
}
