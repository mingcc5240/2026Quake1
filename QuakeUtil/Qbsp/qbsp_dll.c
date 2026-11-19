#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

extern jmp_buf qbsp_err_jmp;
extern char    qbsp_err_msg[512];
extern int     qbsp_main (int argc, char **argv);

// qbsp_dll.c 상단
char qbsp_override_wad[MAX_PATH] = { 0 };

__declspec(dllexport) int QBSP_CompileToMemory (
	const char *map_path,
	const char* wad_path, // [추가] WinQuake에서 넘겨주는 정확한 base.wad 경로
	unsigned char **out_bsp,
	int *out_size,
	char *err_buf,
	int err_buf_size)
{
	char bsp_path[MAX_PATH];
	char map_arg[MAX_PATH];
	char *argv[3];
	FILE *fp;
	long fsize;
	unsigned char *buf;

	*out_bsp = NULL;
	*out_size = 0;
	if (err_buf && err_buf_size > 0) err_buf[0] = 0;

	// 출력 임시 BSP 경로 생성 (예: maps/test.map -> maps/test.bsp)
	strncpy (map_arg, map_path, MAX_PATH - 1);
	map_arg[MAX_PATH - 1] = 0;

	if (wad_path && wad_path[0])
		strncpy(qbsp_override_wad, wad_path, MAX_PATH - 1);
	else
		qbsp_override_wad[0] = 0;

	GetTempPathA (MAX_PATH, bsp_path);
	strcat (bsp_path, "winquake_mem_temp.bsp");

	argv[0] = "qbsp";
	argv[1] = map_arg;
	argv[2] = bsp_path;

	// qbsp 실행 중 Error() 발생 시 여기서 안전하게 캐치
	if (setjmp (qbsp_err_jmp) != 0)
	{
		if (err_buf && err_buf_size > 0)
		{
			strncpy (err_buf, qbsp_err_msg, err_buf_size - 1);
			err_buf[err_buf_size - 1] = 0;
		}
		DeleteFileA (bsp_path);
		return 0;
	}

	// 원본 qbsp 컴파일 실행
	qbsp_main (3, argv);

	// 생성된 BSP 바이너리를 메모리 버퍼로 읽기
	fp = fopen (bsp_path, "rb");
	if (!fp)
	{
		if (err_buf && err_buf_size > 0)
			strncpy (err_buf, "Failed to open compiled temp BSP", err_buf_size - 1);
		return 0;
	}

	fseek (fp, 0, SEEK_END);
	fsize = ftell (fp);
	fseek (fp, 0, SEEK_SET);

	buf = (unsigned char *)malloc (fsize);
	if (!buf)
	{
		fclose (fp);
		DeleteFileA (bsp_path);
		return 0;
	}

	fread (buf, 1, fsize, fp);
	fclose (fp);
	DeleteFileA (bsp_path); // 임시 파일 즉시 삭제

	*out_bsp = buf;
	*out_size = (int)fsize;
	return 1;
}

__declspec(dllexport) void QBSP_FreeBuffer (unsigned char *buf)
{
	if (buf) free (buf);
}