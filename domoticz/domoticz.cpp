// domoticz.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "mainworker.h"

#include <stdio.h>     /* standard I/O functions                         */
#include <sys/types.h> /* various type definitions, like pid_t           */
#include <signal.h>    /* signal name macros, and the signal() prototype */ 

#include "CmdLine.h"

#if defined WIN32
#include "WindowsHelper.h"
#endif

const char *szAppTitle="Domoticz V1.01 (c)2012 GizMoCuz\n";

const char *szHelp=
	"Usage: Domoticz -www port -verbose x\n"
	"\t-www port (for example -www 8080)\n"
	"\t-verbose x (where x=0 is none, x=1 is debug)\n";


MainWorker _mainworker;

void DQuitFunction()
{
	std::cout << "Closing application!..." << std::endl;
	fflush(stdout);
	std::cout << "stopping worker...\n";
	_mainworker.Stop();
}

void catch_intterm(int sig_num)
{
	DQuitFunction();
	exit(0); 
}

#if defined WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
#else
int main(int argc, char**argv)
#endif
{
	std::cout << szAppTitle;

	CCmdLine cmdLine;

	// parse argc,argv 
#if defined WIN32
	cmdLine.SplitLine(__argc, __argv);

#ifndef _DEBUG
	std::cout << "Windows startup delay... waiting 10 seconds..." << std::endl;
	boost::this_thread::sleep(boost::posix_time::seconds(10));
#endif

	#ifdef _DEBUG
		RedirectIOToConsole();
	#endif
#else
	cmdLine.SplitLine(argc, argv);
#endif

	if ((cmdLine.HasSwitch("-h"))||(cmdLine.HasSwitch("--help"))||(cmdLine.HasSwitch("/?")))
	{
		std::cout << szHelp;
		return 0;
	}

	if (cmdLine.HasSwitch("-www"))
	{
		if (cmdLine.GetArgumentCount("-www")!=1)
		{
			std::cout << "Please specify a port" << std::endl;
			return 0;
		}
		std::string wwwport=cmdLine.GetSafeArgument("-www",0,"8080");
		_mainworker.SetWebserverPort(wwwport);
	}

	if (cmdLine.HasSwitch("-verbose"))
	{
		if (cmdLine.GetArgumentCount("-verbose")!=1)
		{
			std::cout << "Please specify a verbose level" << std::endl;
			return 0;
		}
		int Level=atoi(cmdLine.GetSafeArgument("-verbose",0,"").c_str());
		_mainworker.SetVerboseLevel((eVerboseLevel)Level);
	}

	std::cout << "Webserver listning on port: " << _mainworker.GetWebserverPort() << std::endl;
	if (!_mainworker.Start())
		return 0;

	signal(SIGINT, catch_intterm); 
	signal(SIGTERM,catch_intterm);

	/* now, lets get into an infinite loop of doing nothing. */

#if defined WIN32
	InitWindowsHelper(hInstance,hPrevInstance,nShowCmd,DQuitFunction,atoi(_mainworker.GetWebserverPort().c_str()));
	MSG Msg;
	while(GetMessage(&Msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
#else
	for ( ;; )
		boost::this_thread::sleep(boost::posix_time::seconds(1));
#endif
	return 0;
}

