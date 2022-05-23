// w2.cpp : main source file for w2.exe
//

#include "stdafx.h"

#include "globals.h"

#include "UI/MainDlg.h"
#include "VERSION"

#include "ffdecoder/ffdecoder.h"

#ifdef _VLD
#include <vld.h> //  need to install vld-2.6.0-setup.exe.
#endif 
CAppModule _Module;

#if defined(_WIN32) && defined(_DEBUG) 
#define new DEBUG_NEW
#endif

SharedFile  g_main_logger;  // on Windows, 'FILE*' is not thread-safe.


extern "C" const char g_program_name[] = "ffplay_vc";
extern "C" const int program_birth_year = 2003;
extern "C" void just_show_banner();
extern "C" void show_help_default(const char *opt, const char *arg);

typedef std::vector<CAtlStringA> StringArray;
void get_argv(StringArray& argv)
{
	// parse cmdline
	LPWSTR* szArglist;
	int nArgs;
	int i;

	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (NULL == szArglist)
	{
		LOG_WARN("CommandLineToArgvW failed\n");
		return ;
	}
	
	for (i = 0; i < nArgs; i++)
	{
		//printf("%d: %ws\n", i, szArglist[i]);
		CW2A a(szArglist[i]);
		argv.push_back(a.operator LPSTR());
	}

	// Free memory allocated for CommandLineToArgvW arguments.
	LocalFree(szArglist);
}


int Run(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT)
{
	StringArray argv;
	get_argv(argv);

	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	//CMainDlg dlgMain;
	CMainFrame dlgMain;

	if (argv.size() > 1)
	{
		// we can also extend to 'auto open list of files'
		dlgMain.m_auto_open_file = argv[1];
	}

	if(dlgMain.CreateEx() == NULL)
	{
		ATLTRACE(_T("Main dialog creation failed!\n"));
		return 0;
	}

	dlgMain.DragAcceptFiles();

	dlgMain.ShowWindow(nCmdShow);

	int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();
	return nRet;
}


int main(int argc, char* argv[])
{

#ifndef _VLD 
	// 退而求其次
	void enable_crt_heap_dbg();
	enable_crt_heap_dbg();
#endif

	HRESULT hRes = ::CoInitialize(NULL);
	ATLASSERT(SUCCEEDED(hRes));

	AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls

	HINSTANCE hInstance = GetModuleHandle(NULL);

	LOG_INFO("Start. version: %s\n", VERSION );

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	/* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
	avdevice_register_all();
#endif
	avformat_network_init();
	just_show_banner();
	Decoder::onetime_global_init();

	int nRet = Run();

	LOG_INFO("Exiting...\n");

	_Module.Term();
	::CoUninitialize();
	
	avformat_network_deinit();

	return nRet;
}

void enable_crt_heap_dbg()
{
	// Get current flag
	int tmpFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);

	tmpFlag |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF;


	// Set flag to the new value.
	_CrtSetDbgFlag(tmpFlag);
}

void show_help_default(const char *opt, const char *arg)
{
}