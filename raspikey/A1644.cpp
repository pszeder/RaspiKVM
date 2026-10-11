//
// RaspiKey Copyright (c) 2019 George Samartzidis <samartzidis@gmail.com>. All rights reserved.
// You are not allowed to redistribute, modify or sell any part of this file in either 
// compiled or non-compiled form without the author's written permission.
//

#include "A1644.h"
#include <iostream> 
#include "Globals.h"
#include "Logger.h"
#include "KeyboardReportSafety.h"

using namespace std;

static void from_json(const nlohmann::json& j, A1644Settings& p)
{
	p.SwapFnCtrl = j.at("swapFnCtrl").get<bool>();
	p.SwapAltCmd = j.at("swapAltCmd").get<bool>();
}

static void to_json(nlohmann::json& j, const A1644Settings& p)
{
	j = nlohmann::json{ {"swapFnCtrl", p.SwapFnCtrl}, {"swapAltCmd", p.SwapAltCmd} };
}

A1644::A1644()
{
}

A1644::~A1644()
{
}

size_t A1644::ProcessInputReport(uint8_t* buf, size_t len)
{
	if (len != sizeof(A1644HidReport))
		return 0; // Ignore report

	A1644HidReport& inRpt = *reinterpret_cast<A1644HidReport*>(buf);
	
	// Ambiguous hardware/rollover reports cannot identify a chord. Keep the
	// last valid host state until a valid report (including release) arrives.
	// The former workaround invented Ctrl+Shift+T from any all-error report.
	if(!KeyboardKeysValid(&inRpt.Key1, 6))
		return 0;

	// SwapFnCtrl mode
	if (m_Settings.SwapFnCtrl)
	{
		// Physical LCtrl pressed
		if (inRpt.Modifier & Globals::HidLCtrlMask)
		{
			if(!inRpt.Key1) // And is it pressed alone?
				m_FakeFnActive = true;
			inRpt.Modifier &= ~Globals::HidLCtrlMask; //Clear LCtrl modifier
		}
		else // Physical LCtrl not pressed
		{
			if (!inRpt.Key1) // Only unset m_FakeFnActive when there is no other key still being pressed
				m_FakeFnActive = false;
		}

		// Physical Fn pressed?
		if (inRpt.Special & 0x2)
			inRpt.Modifier |= Globals::HidLCtrlMask; // Set LCtrl modifier
		else
			inRpt.Modifier &= ~Globals::HidLCtrlMask; // Clear LCtrl modifier
	}
	else // Not SwapFnCtrl mode
		m_FakeFnActive = inRpt.Special & 0x2;
		
	// Eject Pressed?
	if (inRpt.Special & 0x1)
		inRpt.Key1 = Globals::HidDel; // Translate to Del

	//Process optional Alt-Cmd swap
	if (m_Settings.SwapAltCmd)
	{
		if (inRpt.Modifier & Globals::HidLAltMask)
		{
			inRpt.Modifier &= (uint8_t)~Globals::HidLAltMask;
			inRpt.Modifier |= (uint8_t)Globals::HidLCmdMask;
		}
		else if (inRpt.Modifier & Globals::HidLCmdMask)
		{
			inRpt.Modifier &= (uint8_t)~Globals::HidLCmdMask;
			inRpt.Modifier |= (uint8_t)Globals::HidLAltMask;
		}

		if (inRpt.Modifier & (uint8_t)Globals::HidRAltMask)
		{
			inRpt.Modifier &= (uint8_t)~Globals::HidRAltMask;
			inRpt.Modifier |= (uint8_t)Globals::HidRCmdMask;
		}
		else if (inRpt.Modifier & Globals::HidRCmdMask)
		{
			inRpt.Modifier &= (uint8_t)~Globals::HidRCmdMask;
			inRpt.Modifier |= (uint8_t)Globals::HidRAltMask;
		}
	}

	//Is this a break code for a previously pressed multimedia key?
	if (m_MultimediaKeyActive && !inRpt.Key1)
	{
		m_MultimediaKeyActive = false;

		inRpt.ReportId = 0x2;
		inRpt.Modifier = 0x0;

		return 2;
	}

	//Process Fn+[key] combination 
	if (m_FakeFnActive && (inRpt.Key1 || inRpt.Modifier))
	{
		switch (inRpt.Key1)
		{
		case Globals::HidBackspace: inRpt.Key1 = Globals::HidDel;
			break;
		case Globals::HidLeft: inRpt.Key1 = Globals::HidHome; 
			break;
		case Globals::HidRight: inRpt.Key1 = Globals::HidEnd; 
			break;
		case Globals::HidUp: inRpt.Key1 = Globals::HidPgUp; 
			break;
		case Globals::HidDown: inRpt.Key1 = Globals::HidPgDown; 
			break;
		case Globals::HidEnter: inRpt.Key1 = Globals::HidInsert; 
			break;
		case Globals::HidF1: inRpt.Key1 = Globals::HidF13; 
			break;
		case Globals::HidF2: inRpt.Key1 = Globals::HidF14; 
			break;
		case Globals::HidF3: inRpt.Key1 = Globals::HidF15; 
			break;
		case Globals::HidF4: inRpt.Key1 = Globals::HidF16; 
			break;
		case Globals::HidF5: inRpt.Key1 = Globals::HidF17; 
			break;
		case Globals::HidF6: inRpt.Key1 = Globals::HidF18; 
			break;
		case Globals::HidF7:
			m_MultimediaKeyActive = true;
			inRpt.ReportId = 0x2; inRpt.Modifier = 0x02; //Prev Track
			return 2;
			break;
		case Globals::HidF8:
			m_MultimediaKeyActive = true;
			inRpt.ReportId = 0x2; inRpt.Modifier = 0x08; //Play/Pause
			return 2;
			break;
		case Globals::HidF9:
			m_MultimediaKeyActive = true;
			inRpt.ReportId = 0x2; inRpt.Modifier = 0x01; //Next Track
			return 2;
			break;
		case Globals::HidF10:
			m_MultimediaKeyActive = true;
			inRpt.ReportId = 0x2; inRpt.Modifier = 0x10; //Mute
			return 2;
			break;
		case Globals::HidF11:
			m_MultimediaKeyActive = true;
			inRpt.ReportId = 0x2; inRpt.Modifier = 0x40; //Vol Down
			return 2;
			break;
		case Globals::HidF12:
			m_MultimediaKeyActive = true;
			inRpt.ReportId = 0x2; inRpt.Modifier = 0x20; //Vol Up
			return 2;
			break;
		case Globals::HidKeyP: inRpt.Key1 = Globals::HidPrtScr; 
			break;
		case Globals::HidKeyB: inRpt.Key1 = Globals::HidPauseBreak; 
			break;
		case Globals::HidKeyS: inRpt.Key1 = Globals::HidScrLck; 
			break;
		default:
			if (inRpt.Modifier & Globals::HidLCtrlMask) //Map Fn+LCtrl to RCtrl
			{
				inRpt.Modifier &= (uint8_t)~Globals::HidLCtrlMask; //Clear LCtrl
				inRpt.Modifier |= (uint8_t)Globals::HidRCtrlMask; //Set RCtrl
			}
			else
				return 0; //Ignore all other Fn+[key] combinations
			break;
		}
	}

	return len;
}

size_t A1644::ProcessOutputReport(uint8_t* buf, size_t len)
{	
	if (len != sizeof(Globals::HidgOutputReport))
		return 0;

	return len;
}

void A1644::SetSettings(std::string settings)
{
	try
	{
		auto jsondoc = nlohmann::json::parse(settings);
		m_Settings = jsondoc.get<A1644Settings>();
	}
	catch (const exception& m)
	{
		ErrorMsg("Failed to parse settings data: %s", m.what());
		throw;
	}
}

std::string A1644::GetSettings()
{
	nlohmann::json j = m_Settings;
	return j.dump();
}


