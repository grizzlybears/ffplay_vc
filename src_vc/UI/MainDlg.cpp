#include "stdafx.h"
#include <atldlgs.h>

#include "MainDlg.h"
#include "aboutdlg.h"
#include "globals.h"

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CreateSimpleStatusBar();
	
	// 第一层 
	m_splitter_lr.SetSplitterExtendedStyle(SPLIT_FIXEDBARSIZE| SPLIT_RIGHTALIGNED | SPLIT_NONINTERACTIVE); // 不能拖动
	
	m_hWndClient = m_splitter_lr.Create(m_hWnd, rcDefault
		, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN
		);

	// 第一层 右侧播放列表
	m_playlist_pane.SetPaneContainerExtendedStyle(PANECNT_NOBORDER);
	m_playlist_pane.Create(m_splitter_lr, _T("PlayList"), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_playlist.Create(m_playlist_pane, rcDefault, NULL
		, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | LVS_LIST | LVS_SINGLESEL
		, WS_EX_CLIENTEDGE);
	m_playlist_pane.SetClient(m_playlist);

	
	// 第一层 左侧主区域
	m_splitter_tb.SetSplitterExtendedStyle(SPLIT_FIXEDBARSIZE | SPLIT_BOTTOMALIGNED | SPLIT_NONINTERACTIVE); // 不能拖动

	m_splitter_tb.Create(m_splitter_lr, rcDefault
		, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN
		);
	
	// 第一层装填
	m_splitter_lr.SetSplitterPanes(m_splitter_tb, m_playlist_pane);

	// 第二层 上方视频区域
	m_view.Create(m_splitter_tb, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);

	// 第二层 下方控制面板
	m_ctrl_panel.Create(m_splitter_tb);

	// 第二层装填
	m_splitter_tb.SetSplitterPanes(m_view, m_ctrl_panel);

	UpdateLayout();


	// 设定右侧播放列表宽度
	RECT rc;
	m_splitter_lr.GetSplitterRect(&rc);
	m_splitter_lr.SetSplitterPos(rc.right - rc.left - PLAY_LIST_WIDTH);
	//UISetCheck(ID_VIEW_LISTPANE, 1);
	BOOL b;
	OnListPaneClose(0,0, m_playlist_pane.m_hWnd,b); // don't show 'list pane' on initial	

	//UISetCheck(ID_VIEW_STATUS_BAR, 1);
	PostMessage(WM_FAKE_VIEW_STATUS, 0, 0);  // hide status bar on initial


	// 设定下方控制面板高度
	m_splitter_tb.GetSplitterRect(&rc);
	m_splitter_tb.SetSplitterPos(rc.bottom - rc.top - CTRL_PANEL_HEIGHT);


	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	if (m_auto_open_file.IsEmpty())
	{
		return 0;
	}
		
	OnIdle();
	int r = open_file(m_auto_open_file.GetString());
	if (r)
	{
		return 0;
	}

	play(m_view);

	return 0;
}
LRESULT CMainFrame::OnViewListPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	bool bShow = (m_splitter_lr.GetSinglePaneMode() != SPLIT_PANE_NONE);
	//printf("OnViewListPane: bShow = %d\n", (int)bShow);

	m_splitter_lr.SetSinglePaneMode(bShow ? SPLIT_PANE_NONE : SPLIT_PANE_LEFT);
	UISetCheck(ID_VIEW_LISTPANE, bShow);

	return 0;
}

LRESULT CMainFrame::OnListPaneClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& /*bHandled*/)
{
	if (hWndCtl == m_playlist_pane.m_hWnd)
	{
		m_splitter_lr.SetSinglePaneMode(SPLIT_PANE_LEFT);
		UISetCheck(ID_VIEW_LISTPANE, 0);
	}

	return 0;
}

class AutoDropFinish
{
public: 
    AutoDropFinish(HDROP h)
    { 
        hDrop = h;
    }
    ~AutoDropFinish()
    {
        if (hDrop)
        {
            DragFinish(hDrop);
        }
    }

    HDROP hDrop;

};

LRESULT CMainFrame::OnFileDropped(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{ 
    bHandled = 1;
    LOG_DEBUG("yes files dropped\n");

    HDROP hDrop = (HDROP) wParam; 
    AutoDropFinish always(hDrop );

    // 使用 DragQueryFileA 获得拖入的文件列表。
	UINT  ucount = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
	char path[MAX_PATH] = { 0 };
	if (ucount > 0)
	{
		DragQueryFileA(hDrop, 0, path, MAX_PATH);
		LOG_INFO("first file name:%s\n", path);
	}
	else
	{
		LOG_ERROR("open file fail\n");
	}
	// open_file第一个文件;
	INT_PTR r = open_file(path);
	if (r)
	{
		return 0;
	}
	//播放该文件
	play(m_view);
	return 0;
}


LRESULT CMainFrame::OnFileOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{
	bHandled = 1;

	// TODO: add code to initialize document	

	CSimpleFileDialog ofd(
		TRUE
		,  NULL //LPCTSTR lpszDefExt
		,  NULL //LPCTSTR lpszFileName =
		, 0 // DWORD dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		, "All\0*.*\0H264\0*.264\0TS\0*.ts\0MPEG4\0*.mp4\0"
		);

	INT_PTR r = ofd.DoModal();
	if (IDCANCEL == r)
	{
		return 0;
	}

	r = open_file(ofd.m_szFileName);

	if (r)
	{
		return 0;
	}

	play(m_view);
	
	return 0;
}

int PlayPauseButton::init(int play_img, int pause_img, int init_mode)
{
	m_imlPlay.CreateFromImage(play_img, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_imlPause.CreateFromImage(pause_img, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	switch_mode(init_mode);

	return 0;
}

int PlayPauseButton::switch_mode(int new_mode)
{
	if (BM_INVALID == new_mode )
	{
		if (BM_PLAY == _mode)
		{
			switch_to_pause();
		}
		else if (BM_PAUSE == _mode)
		{
			switch_to_play();
		}
		else
		{
			ATLASSERT(0);
		}
	}
	else if (BM_PLAY == new_mode)
	{
		switch_to_play();
	}
	else if (BM_PAUSE == new_mode)
	{
		switch_to_pause();
	}
	else
	{
		ATLASSERT(0);
	}

	return 0;

}

int PlayPauseButton::switch_to_play()
{
	if (BM_PLAY == _mode)
	{
		return 0;
	}
	SetToolTipText("Play");
	SetImageList(m_imlPlay);
	SetImages(1, 0, 1, 3);

	if (IsWindow())
	{
		Invalidate();
	}

	_mode = BM_PLAY;

	return 0;
}

int PlayPauseButton::switch_to_pause()
{
	if (BM_PAUSE == _mode)
	{
		return 0;
	}

	SetToolTipText("Pause");
	SetImageList(m_imlPause);
	SetImages(1, 0, 1, 3);
	if (IsWindow())
	{
		Invalidate();
	}
	_mode = BM_PAUSE;

	return 0;
}


int SoundMuteButton::init(int img, int init_mode )
{
	m_imlSound.CreateFromImage(img, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	SetImageList(m_imlSound);

	if (BM_SOUND == init_mode)
	{
		switch_to_sound();
	}
	else
	{
		switch_to_mute();
	}

	return 0;
}


int SoundMuteButton::switch_to_sound()
{
	if (BM_SOUND == _mode)
	{
		return 0;
	}

	SetToolTipText("click to mute");	
	SetImages(1, 0, 1);
	if (IsWindow())
	{
		Invalidate();
	}
	_mode = BM_SOUND;

	return 0;
}

int SoundMuteButton::switch_to_mute()
{
	if (BM_MUTE == _mode)
	{
		return 0;
	}

	SetToolTipText("click to play sound");
	SetImages(3, 0, 3);
	if (IsWindow())
	{
		Invalidate();
	}
	_mode = BM_MUTE;

	return 0;
}



LRESULT CCtrlPanel::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	m_txtProgress.Attach(GetDlgItem(IDC_PROGRESS));
	m_txtSpeed.Attach(GetDlgItem(IDC_SPEED));
	m_v_slider.Attach(GetDlgItem(IDC_V_PROGRESS));
	m_a_slider.Attach(GetDlgItem(IDC_A_VOLUMN));
	m_a_slider.SetRange(0, VOLUME_MAX);
	//printf(" a slide range %d - %d\n", (int)0, (int)VOLUME_MAX);

	m_btnPlay.SubclassWindow(GetDlgItem(IDC_PLAY));
	m_btnPlay.init(IDB_BMP_PLAY, IDB_BMP_PAUSE);
	
	m_imlStop.CreateFromImage(IDB_BMP_STOP, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_btnStop.SubclassWindow(GetDlgItem(IDC_STOP));
	m_btnStop.SetToolTipText("Stop");
	m_btnStop.SetImageList(m_imlStop);
	m_btnStop.SetImages(1, 0, 1, 3);

	m_imlSlow.CreateFromImage(IDB_BMP_SLOW, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_btnSlow.SubclassWindow(GetDlgItem(IDC_SLOW));
	m_btnSlow.SetToolTipText("Slow");
	m_btnSlow.SetImageList(m_imlSlow);
	m_btnSlow.SetImages(1,0,  1, 3);

	m_imlFast.CreateFromImage(IDB_BMP_FAST, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_btnFast.SubclassWindow(GetDlgItem(IDC_FAST));
	m_btnFast.SetToolTipText("Fast");
	m_btnFast.SetImageList(m_imlFast);
	m_btnFast.SetImages(1, 0, 1, 3);

	m_imlBack.CreateFromImage(IDB_BMP_STEP_BACK, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_btnBack.SubclassWindow(GetDlgItem(IDC_STEP_BACK));
	m_btnBack.SetToolTipText("Back");
	m_btnBack.SetImageList(m_imlBack);
	m_btnBack.SetImages(1, 0, 1, 3);

	m_imlFoward.CreateFromImage(IDB_BMP_STEP_FOWARD, 23, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);
	m_btnFoward.SubclassWindow(GetDlgItem(IDC_STEP_FOWARD));
	m_btnFoward.SetToolTipText("Foward");
	m_btnFoward.SetImageList(m_imlFoward);
	m_btnFoward.SetImages(1, 0, 1, 3);
		
	m_btnSound.SubclassWindow(GetDlgItem(IDC_SOUND));
	m_btnSound.init(IDB_BMP_SOUND);

	return TRUE;
}

LRESULT CCtrlPanel::OnSound(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	bHandled = 1;

	int sounding = 0;
	_outer->is_playing_sound(&sounding);

	if (sounding)
	{
		_outer->mute();
	}
	else
	{
		_outer->play_sound();		
	}

	return 0;
}

LRESULT CCtrlPanel::OnPlay(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	bHandled = 1;

	if (PlayPauseButton::BM_PLAY == m_btnPlay.get_mode())
	{
		_outer->play(_outer->m_view);
	}
	else
	{
		_outer->pause();
	}

	//m_btnPlay.switch_mode();
	
	return 0;
}


LRESULT CCtrlPanel::OnStop(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	bHandled = 1;

	_outer->stop();

	return 0;
}

LRESULT CCtrlPanel::OnFaster(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{
	bHandled = 1;

	_outer->faster();

	return 0;
}

LRESULT CCtrlPanel::OnSlower(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{
	bHandled = 1;

	_outer->slower();

	return 0;
}

LRESULT CCtrlPanel::OnStepForward(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{	
	bHandled = 1;

	_outer->step_forward();

	return 0;
}

LRESULT CCtrlPanel::OnStepBack(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{
	bHandled = 1;

	_outer->step_back();

	return 0;
}

LRESULT CMainFrame::OnPauseResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& bHandled)
{
	bHandled = 1;
	int play_state = get_state();
	if (PS_PLAYING == play_state)
	{
		pause();
	}
	else if(PS_PAUSED == play_state)
	{
		play(m_view);
	}

	return 0;
}

void CMainFrame::adjust_control_status()
{
	int play_state = get_state();

	if (PS_NOFILE == play_state)
	{
		m_ctrl_panel.m_btnPlay.EnableWindow(0);
		m_ctrl_panel.m_btnPlay.switch_to_play();

		m_ctrl_panel.m_btnStop.EnableWindow(0);

		m_ctrl_panel.m_btnFast.EnableWindow(0);
		m_ctrl_panel.m_btnSlow.EnableWindow(0);

		m_ctrl_panel.m_txtSpeed.SetWindowText("X1");

		m_ctrl_panel.m_btnFoward.EnableWindow(0);
		m_ctrl_panel.m_btnBack.EnableWindow(0);

	}
	else if ( PS_LOADED == play_state)
	{
		m_ctrl_panel.m_btnPlay.EnableWindow(1);
		m_ctrl_panel.m_btnPlay.switch_to_play();

		m_ctrl_panel.m_btnStop.EnableWindow(0);

		m_ctrl_panel.m_btnFast.EnableWindow(0);
		m_ctrl_panel.m_btnSlow.EnableWindow(0);

		m_ctrl_panel.m_txtSpeed.SetWindowText("X1");

		m_ctrl_panel.m_btnFoward.EnableWindow(0);
		m_ctrl_panel.m_btnBack.EnableWindow(0);

	}
	else if ( PS_PLAYING == play_state)
	{
		m_ctrl_panel.m_btnPlay.EnableWindow(1);
		m_ctrl_panel.m_btnPlay.switch_to_pause();

		m_ctrl_panel.m_btnStop.EnableWindow(1);
		
		int speed = 0;
		get_speed(&speed);

		m_ctrl_panel.m_btnFast.EnableWindow(speed < SPEED_MAX);
		m_ctrl_panel.m_btnSlow.EnableWindow(speed > -SPEED_MAX);

		CAtlStringA s;
		s.Format("X%s", exp2(speed).GetString());

		m_ctrl_panel.m_txtSpeed.SetWindowText(s.GetString());

		m_ctrl_panel.m_btnFoward.EnableWindow(1);
		m_ctrl_panel.m_btnBack.EnableWindow(1);

	}
	else if ( PS_PAUSED == play_state)
	{
		m_ctrl_panel.m_btnPlay.EnableWindow(1);
		m_ctrl_panel.m_btnPlay.switch_to_play();

		m_ctrl_panel.m_btnStop.EnableWindow(1);

		m_ctrl_panel.m_btnFast.EnableWindow(0);
		m_ctrl_panel.m_btnSlow.EnableWindow(0);

		m_ctrl_panel.m_btnFoward.EnableWindow(1);
		m_ctrl_panel.m_btnBack.EnableWindow(1);
	}
	else if (PS_STEPPING == play_state)
	{
		m_ctrl_panel.m_btnPlay.EnableWindow(1);
		m_ctrl_panel.m_btnPlay.switch_to_play();

		m_ctrl_panel.m_btnStop.EnableWindow(1);

		m_ctrl_panel.m_btnFast.EnableWindow(0);
		m_ctrl_panel.m_btnSlow.EnableWindow(0);

		m_ctrl_panel.m_txtSpeed.SetWindowText("X1");

		m_ctrl_panel.m_btnFoward.EnableWindow(1);
		m_ctrl_panel.m_btnBack.EnableWindow(1);
	}

	else if ( PS_INVALID == play_state)
	{
		m_ctrl_panel.m_btnPlay.EnableWindow(0);
		m_ctrl_panel.m_btnPlay.switch_to_play();

		m_ctrl_panel.m_btnStop.EnableWindow(0);

		m_ctrl_panel.m_btnFast.EnableWindow(0);
		m_ctrl_panel.m_btnSlow.EnableWindow(0);

		m_ctrl_panel.m_txtSpeed.SetWindowText("X1");

		m_ctrl_panel.m_btnFoward.EnableWindow(0);
		m_ctrl_panel.m_btnBack.EnableWindow(0);
	}

	int sounding = 0;
	is_playing_sound(&sounding);
	if (sounding)
	{
		m_ctrl_panel.m_btnSound.switch_to_sound();
	}
	else
	{
		m_ctrl_panel.m_btnSound.switch_to_mute();
	}

	if (!m_ctrl_panel._a_slider_in_dragging)
	{
		unsigned short vol;
		int r = get_volume(&vol);
		if (!r)
		{
			//printf(" a slide pos = %d\n", (int)vol);
			m_ctrl_panel.m_a_slider.SetPos(vol);
		}
	}
}

void CMainFrame::on_state_changed(int old_state, int new_state)
{
	LOG_DEBUG("'%s' => '%s'\n", decode_play_state(old_state), decode_play_state(new_state));

	if (PS_LOADED == new_state)
	{		
		on_media_file_loaded();
	}
	else if (PS_NOFILE == new_state)
	{
		m_ctrl_panel._v_slider_range = 0;
	}
}

void CMainFrame::on_media_file_loaded()
{
	int cur_pos = 0, total_length = 0;

	get_played_time(&cur_pos);
	get_file_total_time(&total_length);
		
	m_ctrl_panel.refresh_progress(cur_pos, total_length);
	
}

void CMainFrame::on_progress(int  seconds)
{
	int total_length = 0;
	
	get_file_total_time(&total_length);
	
	m_ctrl_panel.PostMessage( WM_PROGRESS ,(WPARAM)seconds, (LPARAM)total_length);

}
void CMainFrame::on_picture_size_got(int w, int h)
{
	PostMessage(WM_PIC_SIZE, (WPARAM)w, (LPARAM)h);
}

LRESULT CCtrlPanel::OnProgress(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = 1;

	refresh_progress((int)wParam, (int)lParam);
	return 0;
}

void CCtrlPanel::refresh_progress(int cur, int total)
{
	if (_v_slider_in_dragging)
	{
		return;
	}

	CAtlStringA sCur, sTotal;

	sCur = seconds_2_hhmmss(cur);
		
	if (total > 0)
	{
		sTotal = seconds_2_hhmmss(total);
	}
	else
	{
		sTotal = "N/A";
	}

	CAtlStringA progress;
	progress.Format("%s / %s", sCur.GetString(), sTotal.GetString());

	m_txtProgress.SetWindowText(progress.GetString());

	// 大华解码库在 close A紧跟着 open B 之后，去 GetPlayPos + GetTotalTime，有可能返回 A 的进度和总长度，导致今后的进度条都不对。然而后续的‘B进度’是对的。
	// 只能牺牲一下性能了		 
	// if (total && !_v_slider_range) 	
	{
		//printf("v slider range: 0 - %d\n", _v_slider_max);
		m_v_slider.SetRange(0, _v_slider_max);
		_v_slider_range = total;
	}

	if (  _v_slider_range)
	{
		int pos = cur * _v_slider_max / _v_slider_range;
		//printf("cur = %d, total = %d, pos: %d\n", cur, _v_slider_range , pos);
		m_v_slider.SetPos(pos);
	}
}

LRESULT CCtrlPanel::OnHScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = 1;

	HWND sender = (HWND)lParam;

	WORD code = LOWORD(wParam);
	WORD pos  = HIWORD(wParam);
	if (sender == m_v_slider.m_hWnd)
	{
		on_v_slider(code, pos);
	}
	else if (sender == m_a_slider.m_hWnd)
	{
		on_a_slider(code, pos);
	}
	else
	{
		LOG_WARN("Ignore HScroll with lparam = 0x%x\n",(LONG)lParam);
	}

	return 0;
}

void CCtrlPanel::on_v_slider(WORD code, WORD pos)
{
	// todo: 
	if (TB_THUMBPOSITION == code)
	{
		_v_slider_in_dragging = 0;
				
		_outer->set_played_time(pos *  _v_slider_range / _v_slider_max);
		//_outer->set_played_time(pos);
	}
	else if (TB_THUMBTRACK == code)
	{
		_v_slider_in_dragging = 1;
	}


}
void CCtrlPanel::on_a_slider(WORD code, WORD pos)
{
	if (TB_THUMBPOSITION == code)
	{
		//printf("a slider => %d\n", (int)pos);
		_a_slider_in_dragging = 0;
		_outer->set_volume(pos);
	}
	else if (TB_THUMBTRACK == code)
	{
		_a_slider_in_dragging = 1;
	}
}
LRESULT CMainFrame::OnPicSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = 1;
	fit_picture_size(wParam, lParam);
	return 0;
}
void CMainFrame::fit_picture_size(int w, int h)
{
	LOG_DEBUG("pic size %d x %d\n", w, h );

	
	CWindow* pView = &m_view;
	// get view rectangle
	CRect rcView;
	pView->GetWindowRect(&rcView);

	// get frame rectangle
	CRect rcFrame;
	this->GetWindowRect(rcFrame); 
	
	// screen  rect
	CRect rcScreen(0, 0, GetSystemMetrics(SM_CXSCREEN),	GetSystemMetrics(SM_CYSCREEN));
	//LOG_DEBUG("screen size %d %d\n", rcScreen.Width(), rcScreen.Height());

	int w_extra, h_extra;  //

	w_extra = rcFrame.Width() - rcView.Width() + 4 ;   // View的 ClientRect 比外框 小 4
	h_extra = rcFrame.Height() - rcView.Height() + 4 +40 ; // todo: '40' 是 taskbar 高度， 但是 taskbar 高度可能有变，taskbar位置也不一定是横的

	int numerator[] = { 1,7,3,2,1,1,1 };
	int denomiator[] = { 1,8,4,3,2,3,4 };

	int i;
	int first_match = -1;   // 
	int first_exact_match = -1; //整除
	int w_target, h_target;

	for (i = 0; i < ARRAY_SIZE(numerator); i++)
	{
		w_target = w * numerator[i] / denomiator[i];
		h_target = h * numerator[i] / denomiator[i];
		int exactly_divide = (
			0 == (w % numerator[i])
			&& 0 == (w % denomiator[i])
			);

		if ((w_target + w_extra) > rcScreen.Width()
			|| (h_target + h_extra) > rcScreen.Height()
			)
		{
			// unmatch
			continue;
		}

		if (first_match < 0)
		{
			first_match = i;
		}

		if (first_exact_match < 0 && exactly_divide)
		{
			first_exact_match = i;
			break;
		}
	}

	if (first_match >= 0)
	{
		LOG_DEBUG("scale ratio: %d/%d\n", numerator[first_match] , denomiator[first_match]);
	}
	else
	{
		LOG_WARN("Could not find perfect scale ratio, no scale.\n");
		return;
	}

	int scale_ratio = first_match;

	if (first_exact_match >= 0 && 
		first_exact_match <= first_match+1  // 如果有整除比率，且缩得不多，就用整除比率
		)
	{
		LOG_DEBUG("choose exact scale ratio: %d/%d\n", numerator[first_exact_match], denomiator[first_exact_match]);
		scale_ratio = first_exact_match;
	}

	w_target = w * numerator[scale_ratio] / denomiator[scale_ratio];
	h_target = h * numerator[scale_ratio] / denomiator[scale_ratio];
	
	rcFrame.right  = rcFrame.left + w_target + rcFrame.Width() - rcView.Width() + 4;
	rcFrame.bottom = rcFrame.top + h_target + rcFrame.Height() - rcView.Height()+ 4;
	
	// move frame!

	this->SetWindowPos(NULL, rcFrame.left, rcFrame.top, rcFrame.Width(), rcFrame.Height(), SWP_NOZORDER);
	//this->CenterWindow();

}

//#define FSTRACE printf
#define FSTRACE 
//////////////////
// Resize frame so view's client area fills the entire screen. Use
// GetSystemMetrics to get the screen size -- the rest is pixel
// arithmetic.
//
void CMainFrame::Maximize()
{
	m_StatusBarShow = ::IsWindowVisible(m_hWndStatusBar);
	m_RightListShow = m_splitter_lr.GetSinglePaneMode() == SPLIT_PANE_NONE;

	BOOL handled;
	if (m_RightListShow)
	{
		FSTRACE("Let's hide right list.\n");
		OnViewListPane(0,0,0, handled);
	}

	if (m_StatusBarShow)
	{
		FSTRACE("Let's hide status bar.\n");
		OnViewStatusBar(0, 0, 0, handled);
	}

	m_splitter_tb.SetSinglePaneMode(SPLIT_PANE_LEFT);
	
	UpdateLayout();


	CWindow* pView = &m_view;

	// get view rectangle
	
	CRect rcv;
	pView->GetWindowRect(&rcv);

	// get frame rectangle
	this->GetWindowRect(m_rcRestore); // save for restore
	const CRect& rcf = m_rcRestore;				// frame rect

	FSTRACE("Frame=(%d,%d) x (%d,%d)\n",
		rcf.left, rcf.top, rcf.Width(), rcf.Height());
	FSTRACE("View =(%d,%d) x (%d,%d)\n",
		rcv.left, rcv.top, rcv.Width(), rcv.Height());

	// now compute new rect
	CRect rc(0, 0, GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN));

	FSTRACE("Scrn =(%d,%d) x (%d,%d)\n",
		rc.left, rc.top, rc.Width(), rc.Height());
	rc.left += rcf.left - rcv.left;
	rc.top += rcf.top - rcv.top;
	rc.right += rcf.right - rcv.right;
	rc.bottom += rcf.bottom - rcv.bottom;

	FSTRACE("New  =(%d,%d) x (%d,%d)\n",
		rc.left, rc.top, rc.Width(), rc.Height());

	// move frame!

	//浮在顶层
	//LONG_PTR ex_style = GetWindowLongPtr(GWL_EXSTYLE);
	//ex_style &= WS_EX_TOPMOST;
	//SetWindowLongPtr(GWL_EXSTYLE, ex_style);
	//this->SetWindowPos(HWND_TOPMOST, rc.left, rc.top, rc.Width(), rc.Height(), SWP_SHOWWINDOW);

	this->SetWindowPos(NULL, rc.left, rc.top,rc.Width(), rc.Height(), SWP_NOZORDER);
}

void CMainFrame::Restore()
{
	//不再浮在顶层
	//LONG_PTR ex_style = GetWindowLongPtr(GWL_EXSTYLE);
	//ex_style &= ~WS_EX_TOPMOST;
	//SetWindowLongPtr(GWL_EXSTYLE, ex_style);

	const CRect& rc = m_rcRestore;
	this->SetWindowPos(NULL, rc.left, rc.top,
		rc.Width(), rc.Height(), SWP_NOZORDER);
	m_rcRestore.SetRectEmpty();

	m_splitter_tb.SetSinglePaneMode(SPLIT_PANE_NONE);

	BOOL handled;
	if (m_RightListShow)
	{
		OnViewListPane(0, 0, 0, handled);
	}

	if (m_StatusBarShow)
	{
		OnViewStatusBar(0, 0, 0, handled);
	}

	UpdateLayout();
}

CSize CMainFrame::GetMaxSize()
{
	CRect rc(0, 0,
		GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	rc.InflateRect(10, 50);
	return rc.Size();
}

LRESULT CMainFrame::OnQuitFullScreen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& bHandled)
{
	bHandled = 1;

	if (!InFullScreenMode())
	{
		return 0;		
	}
	
	Restore();
	
	return 0;
}

LRESULT CVideoView::OnLeftDClick(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	bHandled = 1;

	if (_outer->InFullScreenMode())
	{
		_outer->Restore();
	}
	else
	{
		_outer->Maximize();
	}

	return 0;
}
