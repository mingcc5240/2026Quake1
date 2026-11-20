/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
// cd_win.c -- miniaudio + stb_vorbis 기반 자체 디코딩 BGM 플레이어 (MP3 / OGG / WAV / FLAC)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 1. Quake의 매크로(false, true, byte 등)와 충돌을 방지하기 위해
//    quakedef.h를 포함하기 전에 stb_vorbis와 miniaudio를 먼저 구현합니다.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

// miniaudio 이후에 실제 stb_vorbis 구현부를 포함합니다.
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

// stb_vorbis 내부 매크로가 Quake 코드와 충돌하지 않도록 정리
#ifdef L
#undef L
#endif
#ifdef C
#undef C
#endif
#ifdef R
#undef R
#endif

// 2. 이제 Quake 엔진 헤더를 포함합니다.
#include "quakedef.h"

extern cvar_t bgmvolume;

static qboolean   cdValid = false;
static qboolean   playing = false;
static qboolean   wasPlaying = false;
static qboolean   initialized = false;
static qboolean   enabled = false;
static qboolean   playLooping = false;
static float      cdvolume = -1.0f;
static byte       playTrack = 0;
static byte       remap[100];
static char       currentTrackPath[MAX_OSPATH] = { 0 };

static ma_engine  bgm_engine;
static ma_sound   bgm_sound;
static qboolean   soundLoaded = false;

static void CDAudio_UnloadSound(void)
{
	if (soundLoaded)
	{
		ma_sound_stop(&bgm_sound);
		ma_sound_uninit(&bgm_sound);
		soundLoaded = false;
	}
}

static void CDAudio_SetVolume(float vol)
{
	if (!initialized || !enabled)
		return;

	if (vol < 0.0f) vol = 0.0f;
	if (vol > 1.0f) vol = 1.0f;

	ma_engine_set_volume(&bgm_engine, vol);
}

static qboolean CDAudio_CheckFile(const char* relPath, char* outPath, size_t maxLen)
{
	DWORD attr = GetFileAttributesA(relPath);
	if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		GetFullPathNameA(relPath, (DWORD)maxLen, outPath, NULL);
		return true;
	}
	return false;
}

// OGG -> MP3 -> WAV -> FLAC 순서로 트랙 파일을 탐색합니다.
static qboolean CDAudio_GetTrackPath(byte track, char* outPath, size_t maxLen)
{
	int extIdx, patIdx;
	char relPath[MAX_OSPATH];
	char exeDir[MAX_OSPATH];
	char* lastSlash;

	const char* exts[] = { "ogg", "mp3", "wav", "flac" };
	const char* namePatterns[] = {
		"music/track%02d.%s",
		"music/track%d.%s",
		"music/%02d.%s"
	};

	for (extIdx = 0; extIdx < 4; extIdx++)
	{
		for (patIdx = 0; patIdx < 3; patIdx++)
		{
			char subName[128];
			sprintf(subName, namePatterns[patIdx], (int)track, exts[extIdx]);

			// 1. com_gamedir 기준 (예: ./id1/music/track02.ogg)
			sprintf(relPath, "%s/%s", com_gamedir, subName);
			if (CDAudio_CheckFile(relPath, outPath, maxLen))
				return true;

			// 2. 현재 작업 디렉토리의 id1 폴더 기준
			sprintf(relPath, "id1/%s", subName);
			if (CDAudio_CheckFile(relPath, outPath, maxLen))
				return true;

			// 3. 현재 작업 디렉토리의 music 폴더 기준
			if (CDAudio_CheckFile(subName, outPath, maxLen))
				return true;

			// 4. 실행 파일(.exe)이 위치한 폴더 기준
			if (GetModuleFileNameA(NULL, exeDir, sizeof(exeDir)))
			{
				lastSlash = strrchr(exeDir, '\\');
				if (lastSlash) *lastSlash = '\0';

				sprintf(relPath, "%s\\id1\\%s", exeDir, subName);
				if (CDAudio_CheckFile(relPath, outPath, maxLen))
					return true;

				sprintf(relPath, "%s\\%s", exeDir, subName);
				if (CDAudio_CheckFile(relPath, outPath, maxLen))
					return true;
			}
		}
	}

	return false;
}

void CDAudio_Play(byte track, qboolean looping)
{
	char targetPath[MAX_OSPATH];
	ma_result result;
	ma_uint32 flags;

	//if (!initialized || !enabled)
	//	return;
	if (!initialized || !enabled)
	{
		Con_Printf("CDAudio: Not initialized (%d) or disabled (%d)\n", initialized, enabled);
		return;
	}

	track = remap[track];

	if (playing)
	{
		if (playTrack == track)
			return;
		CDAudio_Stop();
	}

	if (!CDAudio_GetTrackPath(track, targetPath, sizeof(targetPath)))
	{
		Con_Printf("CDAudio: Could not find audio file (OGG/MP3/WAV) for track %02u\n", (unsigned int)track);
		return;
	}

	CDAudio_UnloadSound();

	// 스트리밍 플래그를 사용하여 대용량 OGG/MP3/WAV 파일도 메모리 부담 없이 즉시 재생
	flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
	result = ma_sound_init_from_file(&bgm_engine, targetPath, flags, NULL, NULL, &bgm_sound);
	if (result != MA_SUCCESS)
	{
		Con_Printf("CDAudio: miniaudio failed to load %s (error %d)\n", targetPath, (int)result);
		return;
	}

	soundLoaded = true;
	playLooping = looping;
	playTrack = track;
	playing = true;
	strncpy(currentTrackPath, targetPath, sizeof(currentTrackPath) - 1);

	ma_sound_set_looping(&bgm_sound, looping ? MA_TRUE : MA_FALSE);
	CDAudio_SetVolume(bgmvolume.value);

	if (bgmvolume.value > 0.0f)
	{
		result = ma_sound_start(&bgm_sound);
		if (result != MA_SUCCESS)
		{
			Con_Printf("CDAudio: Failed to start playback (error %d)\n", (int)result);
			CDAudio_UnloadSound();
			playing = false;
			return;
		}
		Con_Printf("CDAudio: Playing track %02u (%s)\n", (unsigned int)track, targetPath);
	}
	else
	{
		wasPlaying = true;
		playing = false;
	}
}

void CDAudio_Stop(void)
{
	if (!initialized || !enabled)
		return;

	CDAudio_UnloadSound();
	wasPlaying = false;
	playing = false;
}

void CDAudio_Pause(void)
{
	if (!initialized || !enabled || !playing || !soundLoaded)
		return;

	ma_sound_stop(&bgm_sound);
	wasPlaying = true;
	playing = false;
}

void CDAudio_Resume(void)
{
	if (!initialized || !enabled || playing || !wasPlaying || !soundLoaded)
		return;

	ma_sound_start(&bgm_sound);
	playing = true;
	wasPlaying = false;
}

static void CD_f(void)
{
	char* command;
	int ret, n;

	if (Cmd_Argc() < 2)
		return;

	command = Cmd_Argv(1);

	if (Q_strcasecmp(command, "on") == 0)
	{
		enabled = true;
		return;
	}
	if (Q_strcasecmp(command, "off") == 0)
	{
		if (playing || wasPlaying)
			CDAudio_Stop();
		enabled = false;
		return;
	}
	if (Q_strcasecmp(command, "reset") == 0)
	{
		enabled = true;
		if (playing || wasPlaying)
			CDAudio_Stop();
		for (n = 0; n < 100; n++)
			remap[n] = n;
		return;
	}
	if (Q_strcasecmp(command, "remap") == 0)
	{
		ret = Cmd_Argc() - 2;
		if (ret <= 0)
		{
			for (n = 1; n < 100; n++)
				if (remap[n] != n)
					Con_Printf("  %u -> %u\n", n, remap[n]);
			return;
		}
		for (n = 1; n <= ret; n++)
			remap[n] = Q_atoi(Cmd_Argv(n + 1));
		return;
	}
	if (Q_strcasecmp(command, "play") == 0)
	{
		CDAudio_Play((byte)Q_atoi(Cmd_Argv(2)), false);
		return;
	}
	if (Q_strcasecmp(command, "loop") == 0)
	{
		CDAudio_Play((byte)Q_atoi(Cmd_Argv(2)), true);
		return;
	}
	if (Q_strcasecmp(command, "stop") == 0)
	{
		CDAudio_Stop();
		return;
	}
	if (Q_strcasecmp(command, "pause") == 0)
	{
		CDAudio_Pause();
		return;
	}
	if (Q_strcasecmp(command, "resume") == 0)
	{
		CDAudio_Resume();
		return;
	}
	if (Q_strcasecmp(command, "info") == 0)
	{
		if (playing)
			Con_Printf("Currently %s track %u (%s)\n", playLooping ? "looping" : "playing", playTrack, currentTrackPath);
		else if (wasPlaying)
			Con_Printf("Paused %s track %u (%s)\n", playLooping ? "looping" : "playing", playTrack, currentTrackPath);
		else
			Con_Printf("No music playing\n");
		Con_Printf("Volume is %.2f\n", cdvolume);
		return;
	}
}

// sys_win.c나 vid_win.c의 윈도우 프로시저에서 호출하는 심볼 호환용 빈 핸들러
LONG CDAudio_MessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return 0;
}

void CDAudio_Update(void)
{
	if (!initialized || !enabled)
		return;

	// 볼륨 슬라이더 변경 감지 및 실시간 반영
	if (bgmvolume.value != cdvolume)
	{
		if (cdvolume <= 0.0f && bgmvolume.value > 0.0f)
		{
			CDAudio_SetVolume(bgmvolume.value);
			CDAudio_Resume();
		}
		else if (bgmvolume.value <= 0.0f && cdvolume > 0.0f)
		{
			CDAudio_Pause();
		}
		else
		{
			CDAudio_SetVolume(bgmvolume.value);
		}
		cdvolume = bgmvolume.value;
	}

	// 1회 재생(play) 모드일 때 곡이 끝났는지 확인하여 정리
	if (playing && soundLoaded && !playLooping)
	{
		if (ma_sound_at_end(&bgm_sound))
		{
			CDAudio_Stop();
		}
	}
}
//
int CDAudio_Init(void)
{
	int n;
	ma_result result;

	// 1. 명령어 등록 (아래쪽 중복 등록은 제거)
	Cmd_AddCommand("cd", CD_f);

	if (cls.state == ca_dedicated)
	{
		Con_Printf("CDAudio: Skipped (dedicated server)\n");
		return -1;
	}

	// VB6 런처 등에서 -nocdaudio를 넘겨도 MP3 BGM은 작동하도록 주석 처리
	/*
	if (COM_CheckParm("-nocdaudio"))
		return -1;
	*/

	// 2. miniaudio 엔진 초기화
	result = ma_engine_init(NULL, &bgm_engine);
	if (result != MA_SUCCESS)
	{
		Con_Printf("CDAudio: Failed to initialize miniaudio engine (error %d)\n", (int)result);
		return -1;
	}

	for (n = 0; n < 100; n++)
		remap[n] = n;

	initialized = true;
	enabled = true;
	cdValid = true;
	cdvolume = bgmvolume.value;

	CDAudio_SetVolume(cdvolume);

	Con_Printf("miniaudio BGM Engine Initialized (OGG/MP3/WAV/FLAC)\n");
	return 0;
}


void CDAudio_Shutdown(void)
{
	if (!initialized)
		return;

	CDAudio_Stop();
	ma_engine_uninit(&bgm_engine);
	initialized = false;
}