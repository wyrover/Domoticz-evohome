/*
 Domoticz, Open Source Home Automation System

 Copyright (C) 2012,2014 Rob Peters (GizMoCuz)

 Domoticz is free software: you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published
 by the Free Software Foundation, either version 3 of the License,
 or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty
 of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Domoticz. If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"
#include "mainworker.h"
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <iostream>
#include "CmdLine.h"
#include "Logger.h"
#include "Helper.h"
#include "WebServer.h"
#include "SQLHelper.h"

#if defined WIN32
	#include "WindowsHelper.h"
	#include <Shlobj.h>
#endif

const char *szHelp=
	"Usage: Domoticz -www port -verbose x\n"
	"\t-www port (for example -www 8080)\n"
#if defined WIN32
	"\t-wwwroot file_path (for example D:\\www)\n"
	"\t-dbase file_path (for example D:\\domoticz.db)\n"
#else
	"\t-wwwroot file_path (for example /opt/domoticz/www)\n"
	"\t-dbase file_path (for example /opt/domoticz/domoticz.db)\n"
#endif
	"\t-verbose x (where x=0 is none, x=1 is debug)\n"
	"\t-startupdelay seconds (default=0)\n"
	"\t-nowwwpwd (in case you forgot the web server username/password)\n"
#if defined WIN32
	"\t-nobrowser (do not start web browser (Windows Only)\n"
#endif
#if defined WIN32
	"\t-log file_path (for example D:\\domoticz.log)\n"
#else
	"\t-log file_path (for example /var/log/domoticz.log)\n"
#endif
	"\t-loglevel (0=All, 1=Status+Error, 2=Error)\n"
	"\t-nocache (do not cache HTML pages (for editing)\n"
	"";

std::string szStartupFolder;
std::string szWWWFolder;
bool bHasInternalTemperature=false;
std::string szInternalTemperatureCommand = "/opt/vc/bin/vcgencmd measure_temp";

std::string szAppVersion="???";

MainWorker m_mainworker;
CLogger _log;
http::server::CWebServer m_webserver;
CSQLHelper m_sql;
bool m_bDontCacheHTMLPages = true;

void DQuitFunction()
{
	_log.Log(LOG_STATUS,"Closing application!...");
	fflush(stdout);
#if defined WIN32
	TrayMessage(NIM_DELETE,NULL);
#endif
	_log.Log(LOG_STATUS,"Stopping worker...");
	m_mainworker.Stop();
}

void catch_intterm(int sig_num)
{
	DQuitFunction();
	exit(EXIT_SUCCESS); 
}

#if defined(_WIN32)
	static size_t getExecutablePathName(char* pathName, size_t pathNameCapacity)
	{
		return GetModuleFileNameA(NULL, pathName, (DWORD)pathNameCapacity);
	}
#elif defined(__linux__) /* elif of: #if defined(_WIN32) */
	#include <unistd.h>
	static size_t getExecutablePathName(char* pathName, size_t pathNameCapacity)
	{
		size_t pathNameSize = readlink("/proc/self/exe", pathName, pathNameCapacity - 1);
		pathName[pathNameSize] = '\0';
		return pathNameSize;
	}
#elif defined(__FreeBSD__)
	#include <sys/sysctl.h>
	static size_t getExecutablePathName(char* pathName, size_t pathNameCapacity)
	{
		int mib[4];
		mib[0] = CTL_KERN;
		mib[1] = KERN_PROC;
		mib[2] = KERN_PROC_PATHNAME;
		mib[3] = -1;
		size_t cb = pathNameCapacity-1;
		sysctl(mib, 4, pathName, &cb, NULL, 0);
		return cb;
	}
#elif defined(__APPLE__) /* elif of: #elif defined(__linux__) */
	#include <mach-o/dyld.h>
	static size_t getExecutablePathName(char* pathName, size_t pathNameCapacity)
	{
		uint32_t pathNameSize = 0;

		_NSGetExecutablePath(NULL, &pathNameSize);

		if (pathNameSize > pathNameCapacity)
			pathNameSize = pathNameCapacity;

		if (!_NSGetExecutablePath(pathName, &pathNameSize))
		{
			char real[PATH_MAX];

			if (realpath(pathName, real) != NULL)
			{
				pathNameSize = strlen(real);
				strncpy(pathName, real, pathNameSize);
			}

			return pathNameSize;
		}

		return 0;
	}
#else /* else of: #elif defined(__APPLE__) */
	#error provide your own getExecutablePathName implementation
#endif /* end of: #if defined(_WIN32) */

void GetAppVersion()
{
	std::string sLine = "";
	std::ifstream infile;

	szAppVersion="???";
	std::string filename=szStartupFolder+"svnversion.h";

	infile.open(filename.c_str());
	if (infile.is_open())
	{
		if (!infile.eof())
		{
			getline(infile, sLine);
			std::vector<std::string> results;
			StringSplit(sLine," ",results);
			if (results.size()==3)
			{
				szAppVersion="1." + results[2];
			}
		}
		infile.close();
	}
}

#if defined WIN32
int WINAPI WinMain(_In_ HINSTANCE hInstance,_In_opt_ HINSTANCE hPrevInstance,_In_ LPSTR lpCmdLine,_In_ int nShowCmd)
#else
int main(int argc, char**argv)
#endif
{
#if defined WIN32
#ifndef _DEBUG
	CreateMutexA(0, FALSE, "Local\\Domoticz"); 
    if(GetLastError() == ERROR_ALREADY_EXISTS) { 
		MessageBox(HWND_DESKTOP,"Another instance of Domoticz is already running!","Domoticz",MB_OK);
        return -1; 
	}
#endif //_DEBUG
	bool bStartWebBrowser=true;
	RedirectIOToConsole();
#endif //WIN32

	szStartupFolder="";
	szWWWFolder="";

	CCmdLine cmdLine;

	// parse argc,argv 
#if defined WIN32
	cmdLine.SplitLine(__argc, __argv);
#else
	cmdLine.SplitLine(argc, argv);
	//ignore pipe errors
	signal(SIGPIPE, SIG_IGN);
#endif

	if (cmdLine.HasSwitch("-approot"))
	{
		if (cmdLine.GetArgumentCount("-approot")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify a APP root path");
			return 0;
		}
		std::string szroot = cmdLine.GetSafeArgument("-approot", 0, "");
		if (szroot.size() != 0)
			szStartupFolder = szroot;
	}

	if (szStartupFolder == "")
	{
#if !defined WIN32
		char szStartupPath[255];
		getExecutablePathName((char*)&szStartupPath,255);
		szStartupFolder=szStartupPath;
		if (szStartupFolder.find_last_of('/')!=std::string::npos)
			szStartupFolder=szStartupFolder.substr(0,szStartupFolder.find_last_of('/')+1);
#else
#ifndef _DEBUG
		char szStartupPath[255];
		char * p;
		GetModuleFileName(NULL, szStartupPath, sizeof(szStartupPath));
		p = szStartupPath + strlen(szStartupPath);

		while (p >= szStartupPath && *p != '\\')
			p--;

		if (++p >= szStartupPath)
			*p = 0;
		szStartupFolder=szStartupPath;
		size_t start_pos = szStartupFolder.find("\\Release\\");
		if(start_pos != std::string::npos) {
			szStartupFolder.replace(start_pos, 9, "\\domoticz\\");
			_log.Log(LOG_STATUS,"%s",szStartupFolder.c_str());
		}
#endif
#endif
	}
	GetAppVersion();
	_log.Log(LOG_STATUS,"Domoticz V%s (c)2012-2014 GizMoCuz",szAppVersion.c_str());

#if !defined WIN32
	//Check if we are running on a RaspberryPi
	std::string sLine = "";
	std::ifstream infile;

	infile.open("/proc/cpuinfo");
	if (infile.is_open())
	{
		while (!infile.eof())
		{
			getline(infile, sLine);
			if (sLine.find("BCM2708")!=std::string::npos)
			{
				_log.Log(LOG_STATUS,"System: Raspberry Pi");
				szInternalTemperatureCommand="/opt/vc/bin/vcgencmd measure_temp";
				bHasInternalTemperature=true;
				break;
			}
		}
		infile.close();
	}

	if (file_exist("/sys/devices/platform/sunxi-i2c.0/i2c-0/0-0034/temp1_input"))
	{
		_log.Log(LOG_STATUS,"System: Cubieboard/Cubietruck");
		szInternalTemperatureCommand="cat /sys/devices/platform/sunxi-i2c.0/i2c-0/0-0034/temp1_input | awk '{ printf (\"temp=%0.2f\\n\",$1/1000); }'";
		bHasInternalTemperature = true;
	}

	_log.Log(LOG_STATUS,"Startup Path: %s", szStartupFolder.c_str());
#endif

	szWWWFolder=szStartupFolder+"www";

	if ((cmdLine.HasSwitch("-h"))||(cmdLine.HasSwitch("--help"))||(cmdLine.HasSwitch("/?")))
	{
		_log.Log(LOG_NORM,szHelp);
		return 0;
	}

	if (cmdLine.HasSwitch("-startupdelay"))
	{
		if (cmdLine.GetArgumentCount("-startupdelay")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify a startupdelay");
			return 0;
		}
		int DelaySeconds=atoi(cmdLine.GetSafeArgument("-startupdelay",0,"").c_str());
		_log.Log(LOG_STATUS,"Startup delay... waiting %d seconds...",DelaySeconds);
		sleep_seconds(DelaySeconds);
	}

	if (cmdLine.HasSwitch("-www"))
	{
		if (cmdLine.GetArgumentCount("-www")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify a port");
			return 0;
		}
		std::string wwwport=cmdLine.GetSafeArgument("-www",0,"8080");
		m_mainworker.SetWebserverPort(wwwport);
	}
	if (cmdLine.HasSwitch("-nowwwpwd"))
	{
		m_mainworker.m_bIgnoreUsernamePassword=true;
	}

	std::string dbasefile=szStartupFolder + "domoticz.db";
#ifdef WIN32
	if (!IsUserAnAdmin())
	{
		char szPath[MAX_PATH];
		HRESULT hr = SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, szPath);
		if (SUCCEEDED(hr))
		{
			std::string sPath=szPath;
			sPath+="\\Domoticz";

			DWORD dwAttr = GetFileAttributes(sPath.c_str());
			BOOL bDirExists=(dwAttr != 0xffffffff && (dwAttr & FILE_ATTRIBUTE_DIRECTORY));
			if (!bDirExists)
			{
				BOOL bRet=CreateDirectory(sPath.c_str(),NULL);
				if (bRet==FALSE) {
					MessageBox(0,"Error creating Domoticz directory in program data folder (%ProgramData%)!!","Error:",MB_OK);
				}
			}
			sPath+="\\domoticz.db";
			dbasefile=sPath;
		}
	}
#endif

	if (cmdLine.HasSwitch("-dbase"))
	{
		if (cmdLine.GetArgumentCount("-dbase")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify a Database Name");
			return 0;
		}
		dbasefile=cmdLine.GetSafeArgument("-dbase",0,"domoticz.db");
	}
	m_sql.SetDatabaseName(dbasefile);

	if (cmdLine.HasSwitch("-wwwroot"))
	{
		if (cmdLine.GetArgumentCount("-wwwroot")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify a WWW root path");
			return 0;
		}
		std::string szroot=cmdLine.GetSafeArgument("-wwwroot",0,"");
		if (szroot.size()!=0)
			szWWWFolder=szroot;
	}

	if (cmdLine.HasSwitch("-verbose"))
	{
		if (cmdLine.GetArgumentCount("-verbose")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify a verbose level");
			return 0;
		}
		int Level=atoi(cmdLine.GetSafeArgument("-verbose",0,"").c_str());
		m_mainworker.SetVerboseLevel((eVerboseLevel)Level);
	}
#if defined WIN32
	if (cmdLine.HasSwitch("-nobrowser"))
	{
		bStartWebBrowser=false;
	}
#endif
	if (cmdLine.HasSwitch("-log"))
	{
		if (cmdLine.GetArgumentCount("-log")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify an output log file");
			return 0;
		}
		std::string logfile=cmdLine.GetSafeArgument("-log",0,"domoticz.log");
		_log.SetOutputFile(logfile.c_str());
	}
	if (cmdLine.HasSwitch("-loglevel"))
	{
		if (cmdLine.GetArgumentCount("-loglevel")!=1)
		{
			_log.Log(LOG_ERROR,"Please specify logfile output level (0=All, 1=Status+Error, 2=Error)");
			return 0;
		}
		int Level=atoi(cmdLine.GetSafeArgument("-loglevel",0,"").c_str());
		_log.SetVerboseLevel((_eLogFileVerboseLevel)Level);
	}
	if (cmdLine.HasSwitch("-nocache"))
	{
		m_bDontCacheHTMLPages = false;
	}

	if (!m_mainworker.Start())
	{
		return 0;
	}

	signal(SIGINT, catch_intterm); 
	signal(SIGTERM,catch_intterm);
	
	/* now, lets get into an infinite loop of doing nothing. */

#if defined WIN32
#ifndef _DEBUG
	RedirectIOToConsole();	//hide console
#endif
	InitWindowsHelper(hInstance,hPrevInstance,nShowCmd,DQuitFunction,atoi(m_mainworker.GetWebserverPort().c_str()),bStartWebBrowser);
	MSG Msg;
	while(GetMessage(&Msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
#else
	for ( ;; )
		sleep_seconds(1);
#endif
	return 0;
}

