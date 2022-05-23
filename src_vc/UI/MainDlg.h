// MainDlg.h : interface of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once


#include <atlframe.h>
#include <atldlgs.h>
#include <atlctrls.h>
#include <atlctrlw.h>
#include <atlctrlx.h>
#include <atlsplit.h>
#include <atltypes.h>

#include "resource.h"
#include "VideoView.h"

#include "Player/SingleFilePlayer.h"

class PlayPauseButton : public CBitmapButton
{
public:
	typedef enum {
		BM_PLAY = 0
		, BM_PAUSE
		, BM_INVALID 
	}ButtonMode;

	int _mode;

	PlayPauseButton()
	{
		_mode = BM_INVALID;
	}

	int get_mode() const
	{
		return _mode;
	}

	CImageList    m_imlPlay;
	CImageList    m_imlPause;

	int init(int play_img, int pause_img, int init_mode = BM_PLAY);

	int switch_mode(int new_mode = BM_INVALID);

	int switch_to_play();
	int switch_to_pause();
};

class SoundMuteButton : public CBitmapButton
{
public:
	typedef enum {
		BM_SOUND = 0
		, BM_MUTE
		, BM_INVALID
	}ButtonMode;

	int _mode;

	SoundMuteButton()
	{
		_mode = BM_INVALID;
	}

	int get_mode() const
	{
		return _mode;
	}

	CImageList    m_imlSound;
	
	int init(int img, int init_mode = BM_SOUND);
		
	int switch_to_sound();
	int switch_to_mute();
};


class CMainFrame;

#define WM_PROGRESS  ( WM_USER + 1)
#define WM_PIC_SIZE  ( WM_USER + 2)
#define WM_FAKE_VIEW_STATUS  ( WM_USER + 3)

class CCtrlPanel : public CDialogImpl<CCtrlPanel>
{
public:
	enum { IDD = IDD_CTRL_PANEL };

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		return CWindow::IsDialogMessage(pMsg);
	}

	CCtrlPanel(CMainFrame * outer)
	{
		_outer = outer;
		_v_slider_range = 0;
		_v_slider_in_dragging = 0;
		_a_slider_in_dragging = 0;

		_v_slider_max = get_slider_max();
	}

	CMainFrame* _outer;

	PlayPauseButton m_btnPlay;
	
	CStatic   m_txtProgress;
	CStatic   m_txtSpeed;

	CBitmapButton m_btnStop;
	CImageList    m_imlStop;

	CBitmapButton m_btnSlow;
	CImageList    m_imlSlow;

	CBitmapButton m_btnFast;
	CImageList    m_imlFast;

	CBitmapButton m_btnBack;
	CImageList    m_imlBack;

	CBitmapButton m_btnFoward;
	CImageList    m_imlFoward;

	SoundMuteButton m_btnSound;

	CTrackBarCtrl m_v_slider;
	CTrackBarCtrl m_a_slider;
	int _v_slider_max;   // max posistion on the control
	int _v_slider_range; // logical range
	int _v_slider_in_dragging;
	int _a_slider_in_dragging;

	BEGIN_MSG_MAP(CCtrlPanel)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_PROGRESS, OnProgress)
		COMMAND_ID_HANDLER(IDC_PLAY, OnPlay)
		COMMAND_ID_HANDLER(IDC_STOP, OnStop)
		COMMAND_ID_HANDLER(IDC_FAST, OnFaster)
		COMMAND_ID_HANDLER(IDC_SLOW, OnSlower)
		COMMAND_ID_HANDLER(IDC_STEP_FOWARD, OnStepForward)
		COMMAND_ID_HANDLER(IDC_STEP_BACK, OnStepBack)
		COMMAND_ID_HANDLER(IDC_SOUND, OnSound)

	END_MSG_MAP()

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnPlay(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	
	LRESULT OnHScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	LRESULT OnFaster(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled);
	LRESULT OnSlower(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled);
	
	LRESULT OnStepForward(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled);
	LRESULT OnStepBack(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled);

	void on_v_slider(WORD code, WORD pos);

	LRESULT OnSound(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	void on_a_slider(WORD code, WORD pos);

	LRESULT OnProgress(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void refresh_progress(int cur, int total);

};



class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CUpdateUI<CMainFrame>,
	public CMessageFilter, public CIdleHandler,
	public SingleFilePlayer
{
public:
	CMainFrame()
		:m_ctrl_panel(this), m_view(this)
	{
	}

	DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME)

	CSplitterWindow m_splitter_lr; // 第一层，左侧主区域，右侧播放列表
	CPaneContainer m_playlist_pane;
	CListViewCtrl  m_playlist;  

	CHorSplitterWindow m_splitter_tb; // 主区域上下分开，下面控制面板，上面视频
	
	CCtrlPanel  m_ctrl_panel;
	CVideoView m_view;

	CAtlStringA m_auto_open_file;
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
			return TRUE;

		return m_view.PreTranslateMessage(pMsg);
	}

	virtual BOOL OnIdle()
	{
		adjust_control_status();
		return FALSE;
	}

	BEGIN_UPDATE_UI_MAP(CMainFrame)		
		UPDATE_ELEMENT(ID_VIEW_STATUS_BAR, UPDUI_MENUPOPUP)
		UPDATE_ELEMENT(ID_VIEW_LISTPANE, UPDUI_MENUPOPUP)
	END_UPDATE_UI_MAP()

	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_CLOSE , OnClose)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_PIC_SIZE, OnPicSize)
		MESSAGE_HANDLER(WM_FAKE_VIEW_STATUS, OnFakeViewStatus)
		MESSAGE_HANDLER(WM_DROPFILES, OnFileDropped)
		
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		COMMAND_ID_HANDLER(ID_FILE_OPEN, OnFileOpen)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_VIEW_LISTPANE, OnViewListPane)
		COMMAND_ID_HANDLER(ID_PANE_CLOSE, OnListPaneClose)
		COMMAND_ID_HANDLER(ID_QUIT_FULLSCREEN, OnQuitFullScreen)
		COMMAND_ID_HANDLER(ID_PAUSE_RESUME, OnPauseResume)
		CHAIN_MSG_MAP(CUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		close_file();

		bHandled = FALSE;
		return 0;
	}
 
	LRESULT OnFileDropped(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
    
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{

		// unregister message filtering and idle updates
		CMessageLoop* pLoop = _Module.GetMessageLoop();
		ATLASSERT(pLoop != NULL);
		pLoop->RemoveMessageFilter(this);
		pLoop->RemoveIdleHandler(this);

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnPicSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);

	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		PostMessage(WM_CLOSE);
		return 0;
	}

	LRESULT OnFileOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	

	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
		::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
		UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
		UpdateLayout();
		return 0;
	}
	LRESULT OnFakeViewStatus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		OnViewStatusBar(0, 0, m_hWndStatusBar, bHandled);
		return 0;
	}
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	
	LRESULT OnViewListPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnListPaneClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& /*bHandled*/);
	
	LRESULT OnPauseResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& bHandled);

	void adjust_control_status();

	void on_media_file_loaded();


	virtual void on_state_changed(int old_state, int new_state);  //overrides SingleFilePlayer

	virtual void on_progress(int  seconds);  // overides DecoderEventCB, usually comes from worker thread (other than UI thread)
	virtual void on_picture_size_got(int w, int h); //overides DecoderEventCB, usually comes from worker thread (other than UI thread)
	void fit_picture_size(int w, int h);


	////// full screen stuff
public:
	void Maximize();
	void Restore();
	BOOL InFullScreenMode()
	{
		return !m_rcRestore.IsRectEmpty();
	}
	CSize GetMaxSize();
	LRESULT OnQuitFullScreen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& bHandled);

	CRect m_rcRestore;
	int   m_RightListShow;
	int   m_StatusBarShow;

};
