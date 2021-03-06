
// MFCQQServerDlg.cpp : 实现文件
//

#include "stdafx.h"
#include "MFCQQServer.h"
#include "MFCQQServerDlg.h"
#include "afxdialogex.h"
#include "tlhelp32.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

string DB_Connector::host_ = "localhost";
string DB_Connector::user_ = "root";
string DB_Connector::passwd_ = "123456";
string DB_Connector::db_ = "mfc_qq_server";

// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg();

    // 对话框数据
#ifdef AFX_DESIGN_TIME
    enum {
        IDD = IDD_ABOUTBOX
    };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CMFCQQServerDlg 对话框
bool findProcessByName(const CString &name) {
    PROCESSENTRY32 pe32 = { sizeof(pe32) };
    HANDLE hp = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    for (BOOL find = Process32First(hp, &pe32); find != 0; find = Process32Next(hp, &pe32)) {
        if (name == pe32.szExeFile)
            return true;
    }
    return false;//整个结束了还没有返回，即代表未找到该进程，所以返回false
}


CMFCQQServerDlg::CMFCQQServerDlg(CWnd* pParent /*=NULL*/)
    : CDialogEx(IDD_MFCQQSERVER_DIALOG, pParent)
    , m_onlineNum(0)
    , p_offlineMsg(NULL)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    listenSocket = NULL;
    m_port = 22783;
}

CMFCQQServerDlg::~CMFCQQServerDlg() {
    for (auto & elem : userSockMap) {
        CString dataToSend = msg.join("", TYPE[Server_is_closed], elem.first);
        sendMsg(dataToSend, elem.second.sock);
    }
    for (auto &elem : userSockMap) {
        elem.second.sock->Close();
        delete elem.second.sock;
    }
}

void CMFCQQServerDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_OnlineNum, m_onlineNum);
}

void CMFCQQServerDlg::addClient()
{
    ServerSocket* pSocket = new ServerSocket(this);
    listenSocket->Accept(*pSocket);
    pSocket->AsyncSelect(FD_READ | FD_WRITE | FD_CLOSE);
}

void CMFCQQServerDlg::receData(ServerSocket* sock)
{
    char buffer[10000];
    if (sock->Receive(buffer, sizeof(buffer)) != SOCKET_ERROR) {
        msg.load(buffer);
        if (msg.type == TYPE[Login]) {
            if (isUserInfoValid(msg.userId, msg.pw)) {
                if (userSockMap.find(msg.userId) == userSockMap.end()) { //说明此时还未保存该用户的socket描述符，可以正常登录
                    CString dataToSend = msg.join(userList, TYPE[UserList], msg.userId);
                    sendMsg(dataToSend, sock);
                    userSockMap[msg.userId] = { sock,1 };
                    updateEvent(msg.userId + "已上线", "");
                    ++m_onlineNum;
                    auto res = p_offlineMsg->pop(msg.userId.GetBuffer()); //GetBuffer是CString类的成员函数，c_str()是string类的成员函数，pop(),取出并删除消息
                    if (res.size() > 0) { //即数据库中有发给该用户的离线消息
                        dataToSend = "";
                        for (auto& it : res) {
                            for (auto&elem : it) {
                                dataToSend += (elem + seperator).c_str();
                            }
                        }
                        dataToSend = msg.join(dataToSend, TYPE[OfflineMsg]);
                        Sleep(100);
                        sendMsg(dataToSend, sock);
                    }
                }
                else {
                    CString dataToSend = msg.join(userList, TYPE[UserList], msg.userId);
                    sendMsg(dataToSend, sock);
                    dataToSend = msg.join("", TYPE[Squezze], msg.userId);
                    sendMsg(dataToSend, userSockMap[msg.userId].sock);
                    userSockMap[msg.userId].sock->Close();
                    delete userSockMap[msg.userId].sock;
                    userSockMap[msg.userId] = { sock,1 };
                    updateEvent(msg.userId + "已在另一处登录", "");
                }
            }
            else {
                CString dataToSend = msg.join("", TYPE[LoginFail], msg.userId);
                sendMsg(dataToSend, sock);
            }
        }
        else if (msg.type == TYPE[Logout]) {
            auto it = userSockMap.find(msg.userId);
            if (it != userSockMap.end()) {
                userSockMap[msg.userId].sock->Close();
                delete userSockMap[msg.userId].sock;
                userSockMap.erase(it);
                updateEvent(msg.userId + "已下线", "");
                --m_onlineNum;
            }
        }
        else if (msg.type == TYPE[ChatMsg]) {
            if (msg.toUser == "服务器") {
                updateEvent(msg.userId, msg.data);
            }
            else {
                auto it = userSockMap.find(msg.toUser);
                if (it != userSockMap.end()) {
                    CString dataToSend = msg.join(msg.data, TYPE[ChatMsg], msg.toUser, msg.userId); //发送的内容 消息类型 消息发给谁 发送者是谁
                    sendMsg(dataToSend, userSockMap[msg.toUser].sock);
                    updateEvent(msg.userId + "给" + msg.toUser, msg.data);
                }
                else {
                    CString dataToSend = msg.join(msg.toUser + "不在线,已转为离线消息", TYPE[Status], msg.userId, "服务器");
                    sendMsg(dataToSend, userSockMap[msg.userId].sock);
                    updateEvent(msg.userId + "给" + msg.toUser, msg.data + "（离线消息）");
                    p_offlineMsg->push(msg.userId.GetBuffer(), msg.toUser.GetBuffer(), msg.data.GetBuffer());
                }
            }
        }
        else if (msg.type == TYPE[Register]) {
            if (userInfoMap.find(msg.userId) == userInfoMap.end()) {
                sendMsg("1", sock);
                CString str = "insert into userinfo(userName,passwd) values('" + msg.userId + "','" + msg.pw + "')";
                pDB_UserInfo->query(str.GetBuffer());
                userInfoMap[msg.userId] = msg.pw; //在map中新增一项，将该用户名和密码对存入map中
                userList += msg.userId + ";"; //在用户列表中新增一条用户名
                for (auto &elem : userSockMap) {
                    CString dataToSend = msg.join(msg.userId, TYPE[AddUserList], elem.first); //格式：消息内容 消息类型 谁能收到（这里针对服务器端来说，就是谁能收到，如果是针对客户端，即谁在发送）从哪里来 去哪里 密码是
                    sendMsg(dataToSend, elem.second.sock);
                }
                updateEvent("", msg.userId + "已注册");
            }
            else {
                sendMsg("0", sock);
                updateEvent("", msg.userId + "尝试注册，但该用户已存在");
            }
        }
        else if (msg.type == TYPE[OnlineState]) {
            if (userSockMap.find(msg.data) != userSockMap.end()) {
                CString dataToSend = msg.join("1", TYPE[OnlineState], msg.userId);
                sendMsg(dataToSend, userSockMap[msg.userId].sock);
            }
            else {
                CString dataToSend = msg.join("0", TYPE[OnlineState], msg.userId);
                sendMsg(dataToSend, userSockMap[msg.userId].sock);
            }
        }
        else if (msg.type == TYPE[I_am_online]) {
            userSockMap[msg.userId].heartbeat = 1;
            //每3秒收到一个I_am_online消息，从而不会实现那个socket优化功能（即用户一段时间不发消息，则服务器自动断开与其的连接） 
        }
        else {
            updateEvent("未知消息", buffer);
        }
        UpdateData(FALSE);
    }
}

bool CMFCQQServerDlg::isUserInfoValid(const CString & user, const CString & pwd)
{
    //std::map<CString, CString>::iterator i = userInfoMap.find(user);
    auto i = userInfoMap.find(user);
    if (i != userInfoMap.end())
        return userInfoMap[user] == pwd;
    return false;
}

int CMFCQQServerDlg::sendMsg(const CString & data, ServerSocket * sock)
{
    if (sock->Send(data, data.GetLength() + 1) == SOCKET_ERROR) {
        MessageBox("发送消息失败：" + ServerSocket::getLastErrorStr(), "错误提示", MB_ICONERROR);
        return SOCKET_ERROR;
    }
    return 0;
}

void CMFCQQServerDlg::updateEvent(const CString & title, const CString & content)
{
    static bool firstRun = 1;
    CString str = getDateTime(firstRun) + " ";
    firstRun = 0;
    if (content == "") {
        str += title + "\r\n";
    }
    else {
        str += title + ": " + content + "\r\n";
    }
    CEdit* pEvent = (CEdit*)GetDlgItem(IDC_RECE_DATA); //获取到界面中一个控件，控件ID由我们自己写
    int lastLine = pEvent->LineIndex(pEvent->GetLineCount() - 1); //获取编辑框最后一行索引，该函数的参数必须是有效的索引值（下标），如有5行则有效的下标是0~4
    pEvent->SetSel(lastLine + 1, lastLine + 2, 0); //选择编辑框最后一行
    pEvent->ReplaceSel(str); //替换所选那一行的内容，本来下一行是没有内容的
}

CString CMFCQQServerDlg::getDateTime(bool haveDate)
{
    if (haveDate) {
        return CTime::GetCurrentTime().Format("%Y-%m-%d %H:%M:%S");
    }
    else {
        return CTime::GetCurrentTime().Format("%H:%M:%S");
    }
}

void CMFCQQServerDlg::modifyStatus(const CString & status, bool _sleep)
{
    HWND h = CreateStatusWindow(WS_CHILD | WS_VISIBLE, status, m_hWnd, 0);
    if (_sleep) {
        Sleep(50);
    }
    ::SendMessage(h, SB_SETBKCOLOR, 0, RGB(0, 125, 205)); //全局函数,则不会调用dlg类的成员函数，而是全局的成员函数
}

BEGIN_MESSAGE_MAP(CMFCQQServerDlg, CDialogEx)
    ON_WM_SYSCOMMAND()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(ID_OpenServer, &CMFCQQServerDlg::OnBnClickedOpenserver)
    ON_BN_CLICKED(IDC_SendMsg, &CMFCQQServerDlg::OnBnClickedSendMsg)
    ON_WM_TIMER()
END_MESSAGE_MAP()


// CMFCQQServerDlg 消息处理程序

BOOL CMFCQQServerDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 将“关于...”菜单项添加到系统菜单中。

    // IDM_ABOUTBOX 必须在系统命令范围内。
    ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
    ASSERT(IDM_ABOUTBOX < 0xF000);

    CMenu* pSysMenu = GetSystemMenu(FALSE);
    if (pSysMenu != NULL) {
        BOOL bNameValid;
        CString strAboutMenu;
        bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
        ASSERT(bNameValid);
        if (!strAboutMenu.IsEmpty()) {
            pSysMenu->AppendMenu(MF_SEPARATOR);
            pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
        }
    }

    // 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
    //  执行此操作
    SetIcon(m_hIcon, TRUE);			// 设置大图标
    SetIcon(m_hIcon, FALSE);		// 设置小图标

    // TODO: 在此添加额外的初始化代码
    if (!findProcessByName("mysqld.exe")) {
        ShellExecute(0, "open", "mysqld", 0, 0, SW_HIDE);
        SetTimer(0, 300, NULL);
    }
    else {
        SetTimer(0, 1, NULL);
    }
    OnBnClickedOpenserver();
    return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CMFCQQServerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == IDM_ABOUTBOX) {
        CAboutDlg dlgAbout;
        dlgAbout.DoModal();
    }
    else {
        CDialogEx::OnSysCommand(nID, lParam);
    }
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CMFCQQServerDlg::OnPaint()
{
    if (IsIconic()) {
        CPaintDC dc(this); // 用于绘制的设备上下文

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // 使图标在工作区矩形中居中
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // 绘制图标
        dc.DrawIcon(x, y, m_hIcon);
    }
    else {
        CDialogEx::OnPaint();
    }
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CMFCQQServerDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CMFCQQServerDlg::OnBnClickedOpenserver()
{
    if (listenSocket != NULL) {
        listenSocket->Close();
        delete listenSocket;
        listenSocket = NULL;
    }
    static bool firstOpen = 1;
    listenSocket = new ServerSocket(this);
    if (!listenSocket->Create(m_port, SOCK_STREAM)) {
        if (!firstOpen) {
            MessageBox("创建套接字失败", "温馨提示");
        }
        firstOpen = 0;
        return;
    }
    if (listenSocket->Listen(UserNumMax)) {
        GetDlgItem(ID_OpenServer)->EnableWindow(0);
        if (!firstOpen)
            MessageBox("开启成功", "温馨提示");
        else
            firstOpen = 0;
    }
    else {
        if (!firstOpen)
            MessageBox("开启失败，请重试:" + ServerSocket::getLastErrorStr(), "温馨提示");
        else
            firstOpen = 0;
    }
}


void CMFCQQServerDlg::OnBnClickedSendMsg()
{
    CString sendData;
    GetDlgItemText(IDC_SendData, sendData);
    if (sendData == "") {
        MessageBox("请先输入消息", "温馨提示");
        return;
    }
    for (auto &elem : userSockMap) {
        CString dataToSend = msg.join(sendData, TYPE[ChatMsg], elem.first, "服务器"); //发送内容 消息类型 谁会收到
        sendMsg(dataToSend, elem.second.sock);
    }
    updateEvent("服务器", sendData); //更新接收区
    SetDlgItemText(IDC_SendData, "");
}


void CMFCQQServerDlg::OnTimer(UINT_PTR nIDEvent)
{
    switch (nIDEvent) {
        case 0:
            KillTimer(0);
            modifyStatus("服务器已开启，等待连接到mysql服务器", 0);
            for (int i = 0; ; ++i) {
                try {
                    pDB_UserInfo = new DB_Connector();
                    pDB_UserInfo->query("show tables like 'UserInfo'");
                    if (pDB_UserInfo->getResult().size() == 0) { //当用户表不存在时
                        string sql = " CREATE TABLE `UserInfo` ("
                            "  `id` int(11) NOT NULL AUTO_INCREMENT  PRIMARY KEY,"
                            "  `userName` varchar(17) NOT NULL UNIQUE,"
                            "  `passwd` varchar(17) NOT NULL,"
                            "  `type` tinyint(4) DEFAULT '0'"
                            " ) ENGINE=InnoDB AUTO_INCREMENT=1 DEFAULT CHARSET=utf8;";
                        pDB_UserInfo->query(sql); //创建表
                    }
                    else {
                        pDB_UserInfo->query("select userName,passwd from UserInfo");
                        auto res = pDB_UserInfo->getResult();
                        userList = "";
                        for (auto& it : res) {
                            userInfoMap[it[0].c_str()] = it[1].c_str();
                            userList += (it[0] + ";").c_str();
                        }
                    }
                    break;
                } catch (std::logic_error& e) {
                    if (i > 15) {
                        MessageBox(e.what());
                        exit(-1);
                    }
                    Sleep(300);
                }
            }
            p_offlineMsg = new DB_OfflineMsg("offline_msg", "offline_msg.log");
            SetTimer(1, 2200, NULL);
            modifyStatus("服务器已开启！", 0);
            break;
        case 1:
            for (auto it = userSockMap.begin(); it != userSockMap.end();) {
                auto itTemp = it++;
                if (!itTemp->second.heartbeat) { //表示该用户一段时间内未发送心跳包
                    updateEvent(itTemp->first + "异常下线", "");
                    --m_onlineNum;
                    UpdateData(FALSE);
                    itTemp->second.sock->Close();
                    delete itTemp->second.sock;
                    userSockMap.erase(itTemp);
                }
                else {
                    itTemp->second.heartbeat = 0;
                }
            }
            break;
    }

    CDialogEx::OnTimer(nIDEvent);
}


BOOL CMFCQQServerDlg::PreTranslateMessage(MSG* pMsg)
{
    int cont_ID = GetWindowLong(pMsg->hwnd, GWL_ID); //获取响应控件消息的ID
    if (cont_ID == IDC_SendData) { //判断是否是要处理的控件的ID
        if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) { //若按下键盘且为enter键
            if (GetKeyState(VK_CONTROL) & 0x80) { //在已经按下enter的前提下又按下了ctrl键（这里0x80是对所有按键的检测）
                                                  //添加换行
                CString str;
                GetDlgItemText(IDC_SendData, str);
                SetDlgItemText(IDC_SendData, str + "\r\n");
                //设置光标位置，将光标移至最后一个位置
                CEdit* pEdit = (CEdit*)GetDlgItem(IDC_SendData);
                int len = str.GetLength() + 2;
                pEdit->SetSel(len - 1, len); //SetSel()用来选中，两参数分别代表选中的起始位置和最终位置
            }
            else {
                OnBnClickedSendMsg();
            }
        }
    }
    return CDialogEx::PreTranslateMessage(pMsg);
}
