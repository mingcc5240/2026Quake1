/*
Copyright (C) 1996-1997 Id Software, Inc.
*/
// cd_win.c -- ID3v2 태그 자동 제거(MCI Error 277 방지)가 적용된 MP3 플레이어

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include "quakedef.h"

extern HWND mainwindow;
extern cvar_t bgmvolume;

static qboolean cdValid = false;
static qboolean playing = false;
static qboolean wasPlaying = false;
static qboolean initialized = false;
static qboolean enabled = false;
static qboolean playLooping = false;
static float    cdvolume;
static byte     playTrack;
static byte     remap[100];
static char     tempMp3Path[MAX_OSPATH] = { 0 };

#define BGM_ALIAS "quake_bgm"

static void CDAudio_CloseMCI(void)
{
	mciSendStringA("stop " BGM_ALIAS, NULL, 0, NULL);
	mciSendStringA("close " BGM_ALIAS, NULL, 0, NULL);
}

static void CDAudio_SetVolume(float vol)
{
	char cmd[128];
	int mciVol;

	if (!initialized || !enabled)
		return;

	if (vol < 0.0f) vol = 0.0f;
	if (vol > 1.0f) vol = 1.0f;

	mciVol = (int)(vol * 1000.0f);
	sprintf(cmd, "setaudio %s volume to %d", BGM_ALIAS, mciVol);
	mciSendStringA(cmd, NULL, 0, NULL);
}

// MCI Error 277 원인이 되는 MP3 앞단 ID3v2 태그(앨범 아트 등)와 빈 패딩을 건너뛰고 순수 오디오 프레임만 추출합니다.
static qboolean CDAudio_PrepareCleanMP3(const char* srcPath, char* outPlayPath, size_t maxLen)
{
	FILE* fpIn, * fpOut;
	unsigned char header[10];
	long audioOffset = 0;
	long fileSize, remain;
	unsigned char buf[16384];
	size_t nRead;
	char tempDir[MAX_OSPATH];

	fpIn = fopen(srcPath, "rb");
	if (!fpIn)
		return false;

	fseek(fpIn, 0, SEEK_END);
	fileSize = ftell(fpIn);
	fseek(fpIn, 0, SEEK_SET);

	// ID3v2 헤더("ID3")가 존재하면 크기(Syncsafe Integer)를 계산해 건너뜀
	if (fread(header, 1, 10, fpIn) == 10)
	{
		if (header[0] == 'I' && header[1] == 'D' && header[2] == '3')
		{
			long tagSize = ((long)(header[6] & 0x7F) << 21) |
				((long)(header[7] & 0x7F) << 14) |
				((long)(header[8] & 0x7F) << 7) |
				((long)(header[9] & 0x7F));
			audioOffset = 10 + tagSize;
		}
	}

	// ID3v2 태그가 아예 없는 순수 MP3라면 원본 경로 그대로 사용
	if (audioOffset == 0 || audioOffset >= fileSize)
	{
		fclose(fpIn);
		strncpy(outPlayPath, srcPath, maxLen);
		return true;
	}

	// 태그 뒤에 이어지는 0x00 패딩을 건너뛰고 실제 MP3 동기화 헤더(0xFF) 위치 탐색
	fseek(fpIn, audioOffset, SEEK_SET);
	while (audioOffset < fileSize)
	{
		int c = fgetc(fpIn);
		if (c == EOF) break;
		if (c == 0xFF)
		{
			audioOffset = ftell(fpIn) - 1;
			break;
		}
	}

	// Windows 임시 폴더에 태그가 제거된 캐시 MP3 생성
	if (GetTempPathA(sizeof(tempDir), tempDir) == 0)
		strcpy(tempDir, ".\\");

	sprintf(tempMp3Path, "%sq1_bgm_clean.mp3", tempDir);

	fpOut = fopen(tempMp3Path, "wb");
	if (!fpOut)
	{
		fclose(fpIn);
		strncpy(outPlayPath, srcPath, maxLen);
		return true;
	}

	fseek(fpIn, audioOffset, SEEK_SET);
	remain = fileSize - audioOffset;
	while (remain > 0 && (nRead = fread(buf, 1, sizeof(buf), fpIn)) > 0)
	{
		fwrite(buf, 1, nRead, fpOut);
		remain -= (long)nRead;
	}

	fclose(fpOut);
	fclose(fpIn);

	if (GetShortPathNameA(tempMp3Path, outPlayPath, (DWORD)maxLen) == 0)
		strncpy(outPlayPath, tempMp3Path, maxLen);

	return true;
}

static qboolean CDAudio_GetTrackPath(byte track, char* outPath, size_t maxLen)
{
	int i;
	char relPath[MAX_OSPATH];
	char fullPath[MAX_OSPATH];
	char exeDir[MAX_OSPATH];
	char* lastSlash;
	DWORD attr;

	const char* subPaths[] = {
		"%s/music/track%02d.mp3",
		"%s/music/track%d.mp3",
		"%s/music/%02d.mp3",
		"id1/music/track%02d.mp3",
		"id1/music/track%d.mp3",
		"id1/music/%02d.mp3",
		"music/track%02d.mp3",
		"music/track%d.mp3",
		"music/%02d.mp3"
	};

	for (i = 0; i < 3; i++)
	{
		sprintf(relPath, subPaths[i], com_gamedir, (int)track);
		attr = GetFileAttributesA(relPath);
		if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
		{
			GetFullPathNameA(relPath, sizeof(fullPath), fullPath, NULL);
			if (GetShortPathNameA(fullPath, outPath, (DWORD)maxLen) == 0)
				strncpy(outPath, fullPath, maxLen);
			return true;
		}
	}

	for (i = 3; i < 9; i++)
	{
		sprintf(relPath, subPaths[i], (int)track);
		attr = GetFileAttributesA(relPath);
		if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
		{
			GetFullPathNameA(relPath, sizeof(fullPath), fullPath, NULL);
			if (GetShortPathNameA(fullPath, outPath, (DWORD)maxLen) == 0)
				strncpy(outPath, fullPath, maxLen);
			return true;
		}
	}

	if (GetModuleFileNameA(NULL, exeDir, sizeof(exeDir)))
	{
		lastSlash = strrchr(exeDir, '\\');
		if (lastSlash) *lastSlash = '\0';

		for (i = 3; i < 9; i++)
		{
			char sub[MAX_OSPATH];
			sprintf(sub, subPaths[i], (int)track);
			sprintf(relPath, "%s\\%s", exeDir, sub);
			attr = GetFileAttributesA(relPath);
			if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
			{
				if (GetShortPathNameA(relPath, outPath, (DWORD)maxLen) == 0)
					strncpy(outPath, relPath, maxLen);
				return true;
			}
		}
	}

	return false;
}

void CDAudio_Play(byte track, qboolean looping)
{
	char rawPath[MAX_OSPATH];
	char playPath[MAX_OSPATH];
	char cmd[MAX_OSPATH + 128];
	MCIERROR err;

	if (!enabled)
		return;

	track = remap[track];

	if (playing)
	{
		if (playTrack == track)
			return;
		CDAudio_Stop();
	}

	if (!CDAudio_GetTrackPath(track, rawPath, sizeof(rawPath)))
	{
		Con_Printf("CDAudio: Could not find MP3 for track %02u\n", (unsigned int)track);
		return;
	}

	CDAudio_CloseMCI();

	// ID3v2 태그를 제거한 클린 MP3 파일 준비
	if (!CDAudio_PrepareCleanMP3(rawPath, playPath, sizeof(playPath)))
	{
		Con_Printf("CDAudio: Failed to read %s\n", rawPath);
		return;
	}

	sprintf(cmd, "open \"%s\" type mpegvideo alias %s", playPath, BGM_ALIAS);
	err = mciSendStringA(cmd, NULL, 0, NULL);
	if (err != 0)
	{
		Con_Printf("CDAudio open failed: [%u] on %s\n", (unsigned int)err, playPath);
		return;
	}

	playLooping = looping;
	playTrack = track;
	playing = true;

	CDAudio_SetVolume(bgmvolume.value);

	sprintf(cmd, "play %s from 0 notify", BGM_ALIAS);
	err = mciSendStringA(cmd, NULL, 0, mainwindow);
	if (err != 0)
	{
		Con_Printf("CDAudio play failed: [%u]\n", (unsigned int)err);
		CDAudio_CloseMCI();
		playing = false;
		return;
	}

	Con_Printf("CDAudio: Playing track %02u\n", (unsigned int)track);

	if (bgmvolume.value == 0.0f)
		CDAudio_Pause();
}

void CDAudio_Stop(void)
{
	if (!enabled || !playing)
		return;

	CDAudio_CloseMCI();
	wasPlaying = false;
	playing = false;
}

void CDAudio_Pause(void)
{
	if (!enabled || !playing)
		return;

	mciSendStringA("pause " BGM_ALIAS, NULL, 0, NULL);
	wasPlaying = playing;
	playing = false;
}

void CDAudio_Resume(void)
{
	char cmd[128];

	if (!enabled || playing || !wasPlaying)
		return;

	sprintf(cmd, "play %s notify", BGM_ALIAS);
	mciSendStringA(cmd, NULL, 0, mainwindow);
	playing = true;
}

static void CD_f(void)
{
	char* command;
	int ret;
	int n;

	if (Cmd_Argc() < 2)
		return;

	command = Cmd_Argv(1);

	if (Q_strcasecmp(command, "on") == 0) { enabled = true; return; }
	if (Q_strcasecmp(command, "off") == 0) { if (playing) CDAudio_Stop(); enabled = false; return; }
	if (Q_strcasecmp(command, "reset") == 0)
	{
		enabled = true;
		if (playing) CDAudio_Stop();
		for (n = 0; n < 100; n++) remap[n] = n;
		return;
	}
	if (Q_strcasecmp(command, "remap") == 0)
	{
		ret = Cmd_Argc() - 2;
		if (ret <= 0)
		{
			for (n = 1; n < 100; n++)
				if (remap[n] != n) Con_Printf("  %u -> %u\n", n, remap[n]);
			return;
		}
		for (n = 1; n <= ret; n++)
			remap[n] = Q_atoi(Cmd_Argv(n + 1));
		return;
	}
	if (Q_strcasecmp(command, "play") == 0) { CDAudio_Play((byte)Q_atoi(Cmd_Argv(2)), false); return; }
	if (Q_strcasecmp(command, "loop") == 0) { CDAudio_Play((byte)Q_atoi(Cmd_Argv(2)), true); return; }
	if (Q_strcasecmp(command, "stop") == 0) { CDAudio_Stop(); return; }
	if (Q_strcasecmp(command, "pause") == 0) { CDAudio_Pause(); return; }
	if (Q_strcasecmp(command, "resume") == 0) { CDAudio_Resume(); return; }
	if (Q_strcasecmp(command, "info") == 0)
	{
		if (playing)
			Con_Printf("Currently %s track %u (MP3)\n", playLooping ? "looping" : "playing", playTrack);
		else if (wasPlaying)
			Con_Printf("Paused %s track %u (MP3)\n", playLooping ? "looping" : "playing", playTrack);
		else
			Con_Printf("No music playing\n");
		Con_Printf("Volume is %f\n", cdvolume);
		return;
	}
}

LONG CDAudio_MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	char cmd[128];

	switch (wParam)
	{
	case MCI_NOTIFY_SUCCESSFUL:
		if (playing)
		{
			if (playLooping)
			{
				sprintf(cmd, "play %s from 0 notify", BGM_ALIAS);
				mciSendStringA(cmd, NULL, 0, mainwindow);
			}
			else
			{
				playing = false;
				CDAudio_CloseMCI();
			}
		}
		break;

	case MCI_NOTIFY_ABORTED:
	case MCI_NOTIFY_SUPERSEDED:
		break;

	case MCI_NOTIFY_FAILURE:
		CDAudio_Stop();
		break;
	}

	return 0;
}

void CDAudio_Update(void)
{
	if (!enabled)
		return;

	if (bgmvolume.value != cdvolume)
	{
		if (cdvolume == 0.0f && bgmvolume.value > 0.0f)
		{
			CDAudio_SetVolume(bgmvolume.value);
			CDAudio_Resume();
		}
		else if (bgmvolume.value == 0.0f && cdvolume > 0.0f)
		{
			CDAudio_Pause();
		}
		else
		{
			CDAudio_SetVolume(bgmvolume.value);
		}
		cdvolume = bgmvolume.value;
	}
}

int CDAudio_Init(void)
{
	int n;

	if (cls.state == ca_dedicated)
		return -1;

	if (COM_CheckParm("-nocdaudio"))
		return -1;

	for (n = 0; n < 100; n++)
		remap[n] = n;

	initialized = true;
	enabled = true;
	cdValid = true;
	cdvolume = bgmvolume.value;

	Cmd_AddCommand("cd", CD_f);

	Con_Printf("MP3 Audio Initialized\n");
	return 0;
}

void CDAudio_Shutdown(void)
{
	if (!initialized)
		return;
	CDAudio_Stop();
	if (tempMp3Path[0])
		DeleteFileA(tempMp3Path);
	initialized = false;
}