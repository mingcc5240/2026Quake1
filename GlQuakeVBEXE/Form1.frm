VERSION 5.00
Begin VB.Form Form1 
   Caption         =   "GLQuake Visual basic 6.0 -made by Min Argon-"
   ClientHeight    =   10230
   ClientLeft      =   60
   ClientTop       =   405
   ClientWidth     =   15210
   LinkTopic       =   "Form1"
   ScaleHeight     =   10230
   ScaleWidth      =   15210
   StartUpPosition =   3  'Windows 기본값
   Begin VB.CommandButton btnOpenDoors 
      Caption         =   "Door Open"
      Height          =   495
      Left            =   12480
      TabIndex        =   3
      Top             =   8520
      Width           =   1575
   End
   Begin VB.CommandButton btnFreeze 
      Caption         =   "Freezing"
      Height          =   495
      Left            =   12480
      TabIndex        =   2
      Top             =   7800
      Width           =   1575
   End
   Begin VB.CommandButton btnMonsterJump 
      Caption         =   "MonsterJump"
      Height          =   615
      Left            =   12360
      TabIndex        =   1
      Top             =   6960
      Width           =   1695
   End
   Begin VB.PictureBox picQuake 
      Height          =   8775
      Left            =   120
      ScaleHeight     =   581
      ScaleMode       =   3  '픽셀
      ScaleWidth      =   693
      TabIndex        =   0
      Top             =   120
      Width           =   10455
   End
End
Attribute VB_Name = "Form1"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

' glquake.dll API 선언
Private Declare Function VB_QuakeInit Lib "glquake.dll" ( _
    ByVal hWnd As Long, _
    ByVal Width As Long, _
    ByVal Height As Long, _
    ByVal BaseDir As String) As Long

Private Declare Sub VB_QuakeFrame Lib "glquake.dll" ()
Private Declare Sub VB_QuakeSetMouseGrab Lib "glquake.dll" (ByVal Grab As Long)
Private Declare Sub VB_QuakeShutdown Lib "glquake.dll" ()
Private Declare Sub VB_QuakeCommand Lib "glquake.dll" (ByVal Cmd As String)

Private Declare Function GetAsyncKeyState Lib "user32" (ByVal vKey As Long) As Integer
Private Declare Sub Sleep Lib "kernel32" (ByVal dwMilliseconds As Long)
Private Declare Function ChDir Lib "msvbvm60.dll" Alias "rtcChangeDir" (ByVal Path As String) As Long

' Form1 상단 선언부에 추가
Private Declare Function VB_QuakeGetEntityInfo Lib "glquake.dll" ( _
    ByVal Index As Long, _
    ByVal OutClassname As String, _
    ByRef X As Single, ByRef Y As Single, ByRef Z As Single, _
    ByRef Health As Single) As Long

Private Declare Function VB_QuakePushAllMonsters Lib "glquake.dll" ( _
    ByVal VX As Single, ByVal VY As Single, ByVal VZ As Single) As Long

Private Declare Sub VB_QuakeFreezeMonsters Lib "glquake.dll" (ByVal Freeze As Long)
Private Declare Function VB_QuakeControlDoors Lib "glquake.dll" (ByVal VZ As Single) As Long
Private Declare Sub VB_QuakeSetEntityPhysics Lib "glquake.dll" ( _
    ByVal Index As Long, ByVal VX As Single, ByVal VY As Single, ByVal VZ As Single)
    
Private m_bRunning As Boolean
Private m_bMouseGrabbed As Boolean

' 컨트롤 동적 생성을 위한 변수 (폼에 직접 안 그려도 코드에서 자동 생성됨)
Private WithEvents btnGod As CommandButton
Attribute btnGod.VB_VarHelpID = -1
Private WithEvents btnWeapons As CommandButton
Attribute btnWeapons.VB_VarHelpID = -1
Private WithEvents btnNoclip As CommandButton
Attribute btnNoclip.VB_VarHelpID = -1
Private WithEvents btnNotarget As CommandButton
Attribute btnNotarget.VB_VarHelpID = -1
Private WithEvents btnQuad As CommandButton
Attribute btnQuad.VB_VarHelpID = -1
Private WithEvents txtCmd As TextBox
Attribute txtCmd.VB_VarHelpID = -1
Private WithEvents btnSendCmd As CommandButton
Attribute btnSendCmd.VB_VarHelpID = -1



Private Sub btnFreeze_Click()
    Static bFrozen As Boolean
    bFrozen = Not bFrozen
    VB_QuakeFreezeMonsters IIf(bFrozen, 1, 0)
    picQuake.SetFocus
End Sub

Private Sub btnMonsterJump_Click()
    Dim nCount As Long
    ' 모든 몬스터에게 Z축(위쪽)으로 400만큼의 초기 속도(v_z)를 부여!
    nCount = VB_QuakePushAllMonsters(0!, 0!, 400!)
    'Me.Caption = nCount & "마리의 몬스터를 공중으로 띄웠습니다!"
    MsgBox nCount & "마리의 몬스터를 공중으로 띄웠습니다!"
    picQuake.SetFocus
End Sub

Private Sub btnOpenDoors_Click()
    ' 모든 문(func_door)과 플랫폼을 위쪽으로 강제 개방
    VB_QuakeControlDoors 300!
    picQuake.SetFocus
End Sub

Private Sub Form_Load()
    ChDrive App.Path
    VBA.ChDir App.Path

    Me.ScaleMode = vbPixels
    Me.Caption = "VB6 GLQuake 800x600 (F5:무적 F6:풀무기 F7:벽통과 F8:투명 F12:마우스잠금)"
    
    ' 800x600 게임 화면 + 우측 치트키 패널 공간 확보 (가로 980, 세로 645)
    Me.Width = 980 * Screen.TwipsPerPixelX
    Me.Height = 645 * Screen.TwipsPerPixelY
    
    ' 게임 화면 PictureBox를 800x600으로 설정
    picQuake.ScaleMode = vbPixels
    picQuake.BorderStyle = 0
    picQuake.Move 0, 0, 800, 600
    picQuake.TabStop = True
    
    ' 우측 치트키 버튼 UI 자동 생성 (X좌표 810으로 배치)
    CreateCheatUI
End Sub

Private Sub CreateCheatUI()
    Set btnGod = Me.Controls.Add("VB.CommandButton", "btnGod")
    btnGod.Move 810, 10, 145, 35
    btnGod.Caption = "무적 모드 (F5)"
    btnGod.Visible = True

    Set btnWeapons = Me.Controls.Add("VB.CommandButton", "btnWeapons")
    btnWeapons.Move 810, 55, 145, 35
    btnWeapons.Caption = "모든 무기+탄약 (F6)"
    btnWeapons.Visible = True

    Set btnNoclip = Me.Controls.Add("VB.CommandButton", "btnNoclip")
    btnNoclip.Move 810, 100, 145, 35
    btnNoclip.Caption = "벽 통과 Noclip (F7)"
    btnNoclip.Visible = True

    Set btnNotarget = Me.Controls.Add("VB.CommandButton", "btnNotarget")
    btnNotarget.Move 810, 145, 145, 35
    btnNotarget.Caption = "몬스터 인식무시 (F8)"
    btnNotarget.Visible = True

    Set btnQuad = Me.Controls.Add("VB.CommandButton", "btnQuad")
    btnQuad.Move 810, 190, 145, 35
    btnQuad.Caption = "쿼드 데미지 4배 (F9)"
    btnQuad.Visible = True

    Set txtCmd = Me.Controls.Add("VB.TextBox", "txtCmd")
    txtCmd.Move 810, 250, 145, 25
    txtCmd.Text = "map e1m1"
    txtCmd.Visible = True

    Set btnSendCmd = Me.Controls.Add("VB.CommandButton", "btnSendCmd")
    btnSendCmd.Move 810, 280, 145, 35
    btnSendCmd.Caption = "명령어 직접 전송"
    btnSendCmd.Visible = True
End Sub

Private Sub Form_Activate()
    Static bInitialized As Boolean
    If bInitialized Then Exit Sub
    bInitialized = True
    
    Me.Show
    DoEvents
    picQuake.SetFocus
    
    ' 800, 600 해상도로 GLQuake 초기화 호출
    If VB_QuakeInit(picQuake.hWnd, 800, 600, App.Path) <> 0 Then
        m_bRunning = True
        m_bMouseGrabbed = False
        RunGameLoop
    Else
        MsgBox "GLQuake 초기화 실패!", vbCritical
        Unload Me
    End If
End Sub
Private Sub RunGameLoop()
    Dim bKeyPressed(1 To 6) As Boolean
    
    Do While m_bRunning
        ' [F12] 마우스 잠금/해제 토글
        If CheckKeyOnce(vbKeyF12, bKeyPressed(1)) Then
            m_bMouseGrabbed = Not m_bMouseGrabbed
            VB_QuakeSetMouseGrab IIf(m_bMouseGrabbed, 1, 0)
            If m_bMouseGrabbed Then picQuake.SetFocus
        End If
        
        ' [F5] 무적 모드 (god)
        If CheckKeyOnce(vbKeyF5, bKeyPressed(2)) Then VB_QuakeCommand "god"
        
        ' [F6] 모든 무기, 탄약, 열쇠 가득 (impulse 9)
        If CheckKeyOnce(vbKeyF6, bKeyPressed(3)) Then VB_QuakeCommand "impulse 9"
        
        ' [F7] 벽 통과 날아다니기 (noclip)
        If CheckKeyOnce(vbKeyF7, bKeyPressed(4)) Then VB_QuakeCommand "noclip"
        
        ' [F8] 몬스터가 플레이어를 못 봄 (notarget)
        If CheckKeyOnce(vbKeyF8, bKeyPressed(5)) Then VB_QuakeCommand "notarget"
        
        ' [F9] 공격력 4배 쿼드 데미지 (impulse 255)
        If CheckKeyOnce(vbKeyF9, bKeyPressed(6)) Then VB_QuakeCommand "impulse 255"
        
        ' Quake 1프레임 실행
        VB_QuakeFrame
        
        DoEvents
        Sleep 1
    Loop
    
    VB_QuakeSetMouseGrab 0
    VB_QuakeShutdown
    Unload Me
End Sub

' 키가 중복 입력되지 않고 한 번만 눌리도록 체크하는 헬퍼 함수
Private Function CheckKeyOnce(ByVal vKey As Long, ByRef bPrevState As Boolean) As Boolean
    If (GetAsyncKeyState(vKey) And &H8000) <> 0 Then
        If Not bPrevState Then
            bPrevState = True
            CheckKeyOnce = True
        End If
    Else
        bPrevState = False
    End If
End Function

' === 우측 VB6 치트 버튼 클릭 이벤트 ===
Private Sub btnGod_Click()
    VB_QuakeCommand "god"
    picQuake.SetFocus
End Sub

Private Sub btnWeapons_Click()
    VB_QuakeCommand "impulse 9"
    picQuake.SetFocus
End Sub

Private Sub btnNoclip_Click()
    VB_QuakeCommand "noclip"
    picQuake.SetFocus
End Sub

Private Sub btnNotarget_Click()
    VB_QuakeCommand "notarget"
    picQuake.SetFocus
End Sub

Private Sub btnQuad_Click()
    VB_QuakeCommand "impulse 255"
    picQuake.SetFocus
End Sub

Private Sub btnSendCmd_Click()
    If Trim$(txtCmd.Text) <> "" Then
        VB_QuakeCommand txtCmd.Text
    End If
    picQuake.SetFocus
End Sub

Private Sub picQuake_Click()
    picQuake.SetFocus
End Sub

Private Sub Form_QueryUnload(Cancel As Integer, UnloadMode As Integer)
    m_bRunning = False
End Sub

