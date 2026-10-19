//
// RaspiKey Copyright (c) 2019 George Samartzidis <samartzidis@gmail.com>. All rights reserved.
// You are not allowed to redistribute, modify or sell any part of this file in either 
// compiled or non-compiled form without the author's written permission.
//

#include <fcntl.h>
#include <cstdio>
#include <unistd.h>
#include <memory.h>
#include <linux/input.h>
#include <string>      
#include <iostream> 
#include <sstream>
#include <cstdint>
#include <cstddef>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <algorithm>
#include <csignal>
#include <tuple>
#include <libgen.h>
#include <sys/time.h>
#include <glob.h>
#include <linux/hidraw.h>
#include "Globals.h"
#include "ReportFilter.h"
#include "ReportFilterAccess.h"
#include "A1644.h"
#include "A1314.h"
#include "HttpApiServer.h"
#include "bluetooth/bt.h"
#include "Logger.h"
#include "GenericReportFilter.h"
#include "Main.h"
#include "KeyMapReportFilter.h"
#ifdef RASPIKEY_MODERN_RUNTIME
#include "MouseBridge.h"
#include "KeyboardPerformance.h"
#include "CpuPerformance.h"
#include "KvmRouter.h"
#endif

using namespace std;
class KeyboardPerformance;
static int ForwardingLoop(IReportFilter** prps,int hidRawFd,int hidgFd,KeyboardPerformance* performance);

HttpApiServer g_ApiServer;
volatile sig_atomic_t g_ExitRequested = 0;
IReportFilter* g_pReportFilters[3] = { nullptr };
std::mutex g_reportFilterMutex;

int main(int argc, char** argv)
{	
	InfoMsg("RaspiKey");

	// Get RaspiKey binary "working" dir
	const ssize_t moduleLength = readlink("/proc/self/exe", Globals::g_szModuleDir, sizeof(Globals::g_szModuleDir) - 1);
	if (moduleLength < 0) return -1;
	Globals::g_szModuleDir[moduleLength] = '\0';
	dirname(Globals::g_szModuleDir); //strip last component from path

	InfoMsg(Globals::FormatString("System uptime: %lu", Globals::GetUptime()).c_str());	

	struct stat info{};
	if (stat(DATA_DIR, &info) != 0 || !S_ISDIR(info.st_mode))
	{
		if (system("mkdir -p " DATA_DIR) != 0)
		{
			ErrorMsg("Cannot access data directory %s", DATA_DIR);
			return -1;
		}
	}

	// Restore data directory
#ifndef RASPIKEY_MODERN_RUNTIME
	if (stat(DATA_ARCHIVE, &info) == 0)
	{
		InfoMsg("Restoring data directory");

		int retval = system(
			"/bin/rm -fr " DATA_DIR "/* &&"\
			"/bin/tar xf " DATA_ARCHIVE " -C " DATA_DIR " .");
			
		if(retval)
		{
			ErrorMsg("Failed to restore data directory %s, system() call failed with error %d", DATA_DIR, retval);
			return -1;
		}
	}

#endif
	if (!StartServices())
	{
		ErrorMsg("StartServices() failed.");
		StopAllServices();
		return -1;
	}

#ifdef RASPIKEY_MODERN_RUNTIME
	CpuPerformance cpu;
	StartKvm();
	MouseBridge mouse;
	mouse.Start();
#endif
	OpenDevicesLoop();
#ifdef RASPIKEY_MODERN_RUNTIME
	mouse.Stop();
	StopKvm();
#endif
	StopAllServices();

	return 0;
}

void SignalHandler(int signo)
{
	if (signo == SIGINT || signo == SIGTERM)
	{
		// Join threads outside the asynchronous signal handler.
		g_ExitRequested = 1;
	}
	else if (signo == SIGALRM) // Timer
	{
		// No asynchronous work needed.
	}
}

bool StartServices()
{
	// Start the Web Api server
	g_ApiServer.Start();

	// Start the bluetooth
	try
	{
		bluetooth::Start();
	}
	catch (exception& ex)
	{
		ErrorMsg("Fatal error: could not initialize the bluetooth device: %s", ex.what());
		return false;
	}

	// Set signal handlers
	signal(SIGINT, SignalHandler);
	signal(SIGTERM, SignalHandler);
	signal(SIGALRM, SignalHandler);

	return true;
}

void OpenDevicesLoop()
{
	while (!g_ExitRequested)
	{
		DeviceDescriptors fds = { };

		// Open devices
		if (OpenKbDevice(fds) < 0)
		{
			sleep(1);
			continue;
		}
		
		if (OpenHidgDevice(fds.hidgFd) < 0)
		{
			close(fds.inputEventFd);
			close(fds.hidRawFd);

			sleep(1);
			continue;
		}

		// Get the unique identifier (bluetooth address) for opened device
		string devId = fds.inputEventDevUniq;
		std::transform(devId.begin(), devId.end(), devId.begin(), ::toupper);
		InfoMsg("Device unique identifier: %s, VID: %x, PID: %x", devId.c_str(), fds.vid, fds.pid);

		// Serialize filter lifetime with web settings access, never with input forwarding.
		std::unique_lock<std::mutex> filtersLock(g_reportFilterMutex);
		// Create the correct keyboard device IReportFilter
		if((fds.vid == 0x5ac && fds.pid == 0x267) || (fds.vid == 0x4c && fds.pid == 0x267)) // These specifically match the A1644
		{
			InfoMsg("Using A1644 report filter");

			g_pReportFilters[0] = new A1644();
		}
		else if (fds.vid == 0x5ac) // Everything else under Apple vendor ID, Assume A1314
		{
			InfoMsg("Using A1314 report filter");

			g_pReportFilters[0] = new A1314();
		}
		else // Everything else is "generic" keyboard
		{
			InfoMsg("Using Generic report filter");

			g_pReportFilters[0] = new GenericReportFilter(); 
		}
		
		// Set keyboard filter settings if file exists
		string path = GetSettingsFilePath(devId);
		string settings;
		if (Globals::FileReadAllText(path, settings))
		{
			try
			{
				g_pReportFilters[0]->SetSettings(settings);
			}
			catch (const exception& m)
			{
				ErrorMsg(m.what());
			}
		}

		// Create KeyMapReportFilter IReportFilter
		g_pReportFilters[1] = new KeyMapReportFilter();

		// Set keymap filter settings if file exists
		path = GetKeymapFilePath(devId);
		if (Globals::FileReadAllText(path, settings))
		{
			try
			{
				g_pReportFilters[1]->SetSettings(settings);
			}
			catch (const exception& m)
			{
				ErrorMsg(m.what());
			}
		}		

		filtersLock.unlock();
		// Start forwarding loop with the established devices
#ifdef RASPIKEY_MODERN_RUNTIME
        {
            KeyboardPerformance performance(fds.inputEventFd);
            ForwardingLoop(g_pReportFilters, fds.hidRawFd, fds.hidgFd, &performance);
        }
#else
        ForwardingLoop(g_pReportFilters, fds.hidRawFd, fds.hidgFd);
#endif

		// This is to prevent the frozen "C" key situation if the user presses Ctrl-C in the raspikey process console.
		if (g_ExitRequested)
		{
			DbgMsg("Sending break scancode before exiting");
			uint8_t buf[16] = { 0 };
			buf[0] = 1;
			write(fds.hidgFd, buf, 9);
		}
		
		// Close devices
#ifdef RASPIKEY_MODERN_RUNTIME
        KvmRelease(false);
#endif
		close(fds.inputEventFd);
		close(fds.hidRawFd);
		close(fds.hidgFd);

		// Delete all previously created IReportFilter instances
		filtersLock.lock();
		for (int k = 0; g_pReportFilters[k] != nullptr; k++) {
			delete g_pReportFilters[k];
			g_pReportFilters[k] = nullptr;
		}
		filtersLock.unlock();

		sleep(1);
	}
}

void StopAllServices()
{	
	g_ExitRequested = true;

	bluetooth::Stop();
	g_ApiServer.Stop();	
}

int ForwardingLoop(IReportFilter** prps,int hidRawFd,int hidgFd)
{
    return ForwardingLoop(prps,hidRawFd,hidgFd,nullptr);
}
static int ForwardingLoop(IReportFilter** prps, int hidRawFd, int hidgFd, KeyboardPerformance* performance)
{
	uint8_t buf[16] = { 0 };

	while(!g_ExitRequested)
	{
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(hidRawFd, &fds);
		FD_SET(hidgFd, &fds);
        int eventFd=-1;
#ifdef RASPIKEY_MODERN_RUNTIME
        if(performance)eventFd=performance->EventFd();
        if(eventFd>=0)FD_SET(eventFd,&fds);
        int ledFd=KvmLedFd();
        if(ledFd>=0)FD_SET(ledFd,&fds);
#endif
		int fdsmax = std::max(std::max(hidRawFd, hidgFd),eventFd);
#ifdef RASPIKEY_MODERN_RUNTIME
        fdsmax=std::max(fdsmax,ledFd);
#endif

		struct timeval tv{}; 
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		const int ret = select(fdsmax + 1, &fds, nullptr, nullptr, &tv);
		if (ret == -1)
		{
			ErrorMsg("Error select(): %s", strerror(errno));
			if (errno == EINTR) // Interrupted system call due to external signal (we should retry)
				continue;
			return -1; // Any other error, exit the loop
		}

#ifdef RASPIKEY_MODERN_RUNTIME
        if(performance){performance->Drain();performance->Tick();}
#endif
		// Direction: hidRawFd -> hidgFd
		if (FD_ISSET(hidRawFd, &fds))
		{
			ssize_t len = read(hidRawFd, buf, sizeof(buf));
			if (len <= 0)
			{
				ErrorMsg("Error read: %s", strerror(errno));
				return -1;
			}
					
#ifdef RASPIKEY_MODERN_RUNTIME
            timespec readClock{};clock_gettime(CLOCK_MONOTONIC,&readClock);
            uint64_t readAt=uint64_t(readClock.tv_sec)*1000000+readClock.tv_nsec/1000;
#endif
			DbgMsg("[in] -> %s", Globals::FormatBuffer(buf, len).c_str());

			// Process input report with each IReportFilter
			for (int k = 0; prps[k] != nullptr; k++)
			{
				len = prps[k]->ProcessInputReport(buf, len); // Process report with filter
			}			

			if (len > 0) // Should we suppress this report?
			{
				DbgMsg("[in] ~> %s", Globals::FormatBuffer(buf, len).c_str());

#ifdef RASPIKEY_MODERN_RUNTIME
                timespec filterClock{};clock_gettime(CLOCK_MONOTONIC,&filterClock);
                uint64_t filteredAt=uint64_t(filterClock.tv_sec)*1000000+filterClock.tv_nsec/1000;
#endif
#ifdef RASPIKEY_MODERN_RUNTIME
                len = KvmForward(false,buf,len);
#else
				len = write(hidgFd, buf, len); // Transfer report to hidg device
#endif
				if (len < 0)
				{
					ErrorMsg("Error write: %s", strerror(errno));
					return -1;
				}
#ifdef RASPIKEY_MODERN_RUNTIME
                timespec queuedClock{};clock_gettime(CLOCK_MONOTONIC,&queuedClock);
                if(performance)performance->Report(readAt,filteredAt,uint64_t(queuedClock.tv_sec)*1000000+queuedClock.tv_nsec/1000);
#endif
			}
		}

		// Direction: host LED state -> physical keyboard.
#ifdef RASPIKEY_MODERN_RUNTIME
        if(FD_ISSET(hidgFd,&fds)){
            ssize_t size=read(hidgFd,buf,sizeof(buf));
            if(size==2&&buf[0]==1)KvmHostLed(false,buf[1]);
            else if(size<=0)return -1;
        }
        if(ledFd>=0&&FD_ISSET(ledFd,&fds)&&KvmReadLed(buf)){
            size_t size=2;
            for(int k=0;prps[k]!=nullptr;++k)size=prps[k]->ProcessOutputReport(buf,size);
            if(size&&write(hidRawFd,buf,size)<0)return -1;
        }
#else
		if (FD_ISSET(hidgFd, &fds))
		{
			ssize_t len = read(hidgFd, buf, sizeof(buf));
			if (len <= 0)
			{
				ErrorMsg("Error read: %s", strerror(errno));
				return -1;
			}

			DbgMsg("[out] -> %s", Globals::FormatBuffer(buf, len).c_str());

			// Process output report with each IReportFilter
			for (int k = 0; prps[k] != nullptr; k++)
			{
				len = prps[k]->ProcessOutputReport(buf, len); // Process report with filter
			}

			if (len > 0) // Should we suppress this report?
			{
				DbgMsg("[out] ~> %s", Globals::FormatBuffer(buf, len).c_str());

				len = write(hidRawFd, buf, len);
				if (len < 0)
				{
					ErrorMsg("Error write: %s", strerror(errno));
					return -1;
				}
			}
		}
#endif
	}

	return 0;
}

int OpenKbDevice(DeviceDescriptors& fds)
{
#ifdef RASPIKEY_MODERN_RUNTIME
	// Match the Bluetooth keyboard and hidraw node by their kernel-provided identity.
	glob_t events{};
	if (glob("/dev/input/event*", 0, nullptr, &events) != 0) return -1;
	for (size_t i = 0; i < events.gl_pathc; ++i)
	{
		int eventFd = open(events.gl_pathv[i], O_RDWR | O_CLOEXEC);
		if (eventFd < 0) continue;
		input_id id{};
		unsigned long keys[(KEY_MAX / (8 * sizeof(unsigned long))) + 1]{};
		char unique[256]{};
		ioctl(eventFd, EVIOCGID, &id);
		ioctl(eventFd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		ioctl(eventFd, EVIOCGUNIQ(sizeof(unique)), unique);
		const bool keyboard = (keys[KEY_A / (8 * sizeof(unsigned long))] &
			(1UL << (KEY_A % (8 * sizeof(unsigned long))))) != 0;
		if (id.bustype != BUS_BLUETOOTH || !keyboard || !unique[0])
		{ close(eventFd); continue; }
		glob_t rawNodes{};
		int rawFd = -1;
		if (glob("/dev/hidraw*", 0, nullptr, &rawNodes) == 0)
		{
			for (size_t j = 0; j < rawNodes.gl_pathc; ++j)
			{
				int candidate = open(rawNodes.gl_pathv[j], O_RDWR | O_CLOEXEC);
				if (candidate < 0) continue;
				hidraw_devinfo rawInfo{};
				char rawUnique[256]{};
				ioctl(candidate, HIDIOCGRAWINFO, &rawInfo);
				ioctl(candidate, HIDIOCGRAWUNIQ(sizeof(rawUnique)), rawUnique);
				if (rawInfo.bustype == BUS_BLUETOOTH && rawInfo.vendor == id.vendor &&
					rawInfo.product == id.product && strcasecmp(unique, rawUnique) == 0)
				{ rawFd = candidate; break; }
				close(candidate);
			}
		}
		globfree(&rawNodes);
		if (rawFd < 0 || ioctl(eventFd, EVIOCGRAB, 1) < 0)
		{ if (rawFd >= 0) close(rawFd); close(eventFd); continue; }
		char name[256]{}, phys[256]{};
		ioctl(eventFd, EVIOCGNAME(sizeof(name)), name);
		ioctl(eventFd, EVIOCGPHYS(sizeof(phys)), phys);
		fds.inputEventFd = eventFd;
		fds.hidRawFd = rawFd;
		fds.inputEventDevName = name;
		fds.inputEventDevPhys = phys;
		fds.inputEventDevUniq = unique;
		fds.vid = id.vendor;
		fds.pid = id.product;
		globfree(&events);
		return 0;
	}
	globfree(&events);
	return -1;
#else

	fds.inputEventFd = open(EVENT_DEV_PATH, O_RDWR);
	if (fds.inputEventFd < 0)
	{
		InfoMsg("Failed to open() " EVENT_DEV_PATH ": %s", strerror(errno));
		
		return -1;
	}

	char szDevName[256] = "";
	ioctl(fds.inputEventFd, EVIOCGNAME(sizeof(szDevName)), szDevName);
	fds.inputEventDevName = szDevName;

	char szDevPhys[256] = "";
	ioctl(fds.inputEventFd, EVIOCGPHYS(sizeof(szDevPhys)), szDevPhys); //BT ctrl address
	fds.inputEventDevPhys = szDevPhys;

	char szDevUniq[256] = ""; 
	ioctl(fds.inputEventFd, EVIOCGUNIQ(sizeof(szDevUniq)), szDevUniq); //Dev BT address
	fds.inputEventDevUniq = szDevUniq;

	struct input_id deviceId = { 0 };
	ioctl(fds.inputEventFd, EVIOCGID, &deviceId);
	fds.vid = deviceId.vendor;
	fds.pid = deviceId.product;

	int res = ioctl(fds.inputEventFd, EVIOCGRAB, 1);
	if (res < 0)
	{
		InfoMsg("Failed to get exclusive access on " EVENT_DEV_PATH ": %s", strerror(errno));
		
		close(fds.inputEventFd);
		fds.inputEventFd = -1;

		return -1;
	}

	fds.hidRawFd = open(HIDRAW_DEV_PATH, O_RDWR);
	if (fds.hidRawFd < 0)
	{
		InfoMsg("Failed to open() " HIDRAW_DEV_PATH ": %s", strerror(errno));

		close(fds.inputEventFd);
		fds.inputEventFd = -1;

		return -1;
	}

	return 0;
#endif
}

int OpenHidgDevice(int& hidgFd)
{
	hidgFd = open(HIDG_DEV_PATH, O_RDWR);
	if (hidgFd == -1)
	{
		InfoMsg("Failed to open() " HIDG_DEV_PATH ": %s", strerror(errno));

		return -1;
	}

	return 0;
}

std::string GetKeymapFilePath(const std::string& devId)
{
	string addrUpper = devId;
	std::transform(addrUpper.begin(), addrUpper.end(), addrUpper.begin(), ::toupper);

	return Globals::FormatString(DATA_DIR "/%s.keymap", addrUpper.c_str());
}

std::string GetSettingsFilePath(const std::string& devId)
{
	string addrUpper = devId;
	std::transform(addrUpper.begin(), addrUpper.end(), addrUpper.begin(), ::toupper);

	return Globals::FormatString(DATA_DIR "/%s.settings", addrUpper.c_str());
}

void DeleteKeyMap(const std::string& devId)
{
	const string path = GetKeymapFilePath(devId);

	Globals::DeleteFile(path);
	WriteFilterSettings(g_reportFilterMutex, g_pReportFilters[1], "");
}

bool HasKeyMap(const std::string& devId)
{	
	const string path = GetKeymapFilePath(devId);

	return access(path.c_str(), F_OK) != -1;
}

string GetKeyMap(const std::string& devId)
{
	return ReadFilterSettings(g_reportFilterMutex, g_pReportFilters[1]);
}

void SetKeyMap(const std::string& devId, const std::string& json)
{
	// Will fail if the data is invalid
	try
	{
		WriteFilterSettings(g_reportFilterMutex, g_pReportFilters[1], json);
	}
	catch (const exception& m)
	{
		ErrorMsg("Failed to set keymap: %s", m.what());
		throw;
	}

	// Write validated keymap file
	const string path = GetKeymapFilePath(devId);
	if (!Globals::FileWriteAllText(path, json))
		throw runtime_error("Failed to write the keymap file");
}

std::string GetSettings(const std::string& devId)
{
	return ReadFilterSettings(g_reportFilterMutex, g_pReportFilters[0]);
}

void SetSettings(const std::string& devId, const std::string& json)
{
	// Will fail if the data is invalid
	try
	{
		WriteFilterSettings(g_reportFilterMutex, g_pReportFilters[0], json);
	}
	catch (const exception& m)
	{
		ErrorMsg("Failed to set device settings: %s", m.what());
		throw;
	}

	// Write validated settings file
	const string path = GetSettingsFilePath(devId);
	if (!Globals::FileWriteAllText(path, json))
		throw runtime_error("Failed to write device settings file");
}

// Dummy methods to avoid static linking issues with libsystemd.a
extern "C" int sd_listen_fds(int unset_environment) { return 0;  }
extern "C" int sd_is_socket(int fd, int family, int type, int listening) { return 0; }

